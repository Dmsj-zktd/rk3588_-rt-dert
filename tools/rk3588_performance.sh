#!/usr/bin/env bash
# Configure and verify the maximum supported compute DVFS state on RK3588.

set -euo pipefail

readonly CPU_POLICIES=(
  /sys/devices/system/cpu/cpufreq/policy0
  /sys/devices/system/cpu/cpufreq/policy4
  /sys/devices/system/cpu/cpufreq/policy6
)
readonly NPU_DEVFREQ=/sys/class/devfreq/fdab0000.npu
readonly DMC_DEVFREQ=/sys/class/devfreq/dmc

for policy in "${CPU_POLICIES[@]}"; do
  sudo -n sh -c "echo performance > '${policy}/scaling_governor'"
done
sudo -n sh -c "echo performance > '${NPU_DEVFREQ}/governor'"
sudo -n sh -c "echo performance > '${DMC_DEVFREQ}/governor'"
sleep 0.2

failed=0
for policy in "${CPU_POLICIES[@]}"; do
  governor=$(cat "${policy}/scaling_governor")
  current=$(cat "${policy}/scaling_cur_freq")
  maximum=$(cat "${policy}/cpuinfo_max_freq")
  printf '%s governor=%s current=%s max=%s\n' "${policy}" "${governor}" "${current}" "${maximum}"
  [[ "${governor}" == performance && "${current}" == "${maximum}" ]] || failed=1
done

for devfreq in "${NPU_DEVFREQ}" "${DMC_DEVFREQ}"; do
  governor=$(cat "${devfreq}/governor")
  current=$(cat "${devfreq}/cur_freq")
  maximum=$(cat "${devfreq}/max_freq")
  printf '%s governor=%s current=%s max=%s\n' "${devfreq}" "${governor}" "${current}" "${maximum}"
  [[ "${governor}" == performance && "${current}" == "${maximum}" ]] || failed=1
done

for zone in /sys/class/thermal/thermal_zone*; do
  type=$(cat "${zone}/type" 2>/dev/null || true)
  temp=$(cat "${zone}/temp" 2>/dev/null || true)
  [[ -n "${type}" && -n "${temp}" ]] && printf 'thermal %s=%s.%s C\n' "${type}" "$((temp / 1000))" "$((temp % 1000))"
done

if (( failed )); then
  echo "ERROR: not all compute domains reached their maximum supported OPP" >&2
  exit 1
fi
echo "OK: RK3588 compute domains are at their maximum supported OPP"
