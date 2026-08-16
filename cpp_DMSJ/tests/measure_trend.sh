#!/bin/bash
# ============================================================================
# measure_trend.sh - 板端 CPU 时间序列采样（任务 8 回归分析）
# 用法: bash tests/measure_trend.sh <duration_s> <log_name> <rtdetr_pipeline 参数...>
# 每 5s 输出一次 cpu_pct（全线程 utime+stime 增量 / 5s 墙钟，USER_HZ=100）
# 与当前 VmHWM；结束时输出峰值 RSS 并 SIGTERM 优雅退出。
# ============================================================================
set -u

DUR="$1"
LOG="$2"
shift 2

MODEL="${MODEL_PATH:-/home/neardi/Workspace_Codex/models/RT-DETR-RK3588-Models/.rknn/rtdetr_i8.rknn}"
BUILD_DIR="${PROJ_BUILD:-/home/neardi/Workspace_Codex/rk3588_-rt-detr/cpp_DMSJ/build}"
cd "$BUILD_DIR" || exit 2

./rtdetr_pipeline -m "$MODEL" "$@" > "$LOG" 2>&1 &
PID=$!

peak=0
t0=$SECONDS
prev=""
while kill -0 "$PID" 2>/dev/null && [ $((SECONDS - t0)) -lt "$DUR" ]; do
	u=$(awk '{u+=$14;s+=$15} END{print u+s}' /proc/$PID/task/*/stat 2>/dev/null)
	rss=$(awk '/VmHWM/{print $2}' /proc/$PID/status 2>/dev/null)
	el=$((SECONDS - t0))
	if [ -n "$u" ] && [ -n "$prev" ] && [ "$el" -gt 0 ]; then
		cpu=$(awk -v tk="$((u - prev))" -v el="5" 'BEGIN{printf "%.0f", tk/el}')
		echo "t=${el}s cpu_pct=$cpu rss_kb=$rss"
	fi
	prev="$u"
	if [ -n "$rss" ] && [ "$rss" -gt "$peak" ]; then
		peak=$rss
	fi
	sleep 5
done

kill -TERM "$PID" 2>/dev/null
wait "$PID" 2>/dev/null
echo "PEAK_RSS_KB=$peak"
