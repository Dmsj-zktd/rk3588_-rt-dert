#!/bin/bash
# ============================================================================
# measure_trend_xorg.sh - 采样 rtdetr_pipeline 与 Xorg 的 CPU（显示路径开销排查）
# 用法: bash tests/measure_trend_xorg.sh <duration_s> <log_name> <args...>
# ============================================================================
set -u

DUR="$1"
LOG="$2"
shift 2

MODEL="${MODEL_PATH:-/home/neardi/Workspace_Codex/models/RT-DETR-RK3588-Models/.rknn/rtdetr_i8.rknn}"
BUILD_DIR="${PROJ_BUILD:-/home/neardi/Workspace_Codex/rk3588_-rt-detr/cpp_DMSJ/build}"
cd "$BUILD_DIR" || exit 2

DISPLAY=:0 ./rtdetr_pipeline -m "$MODEL" "$@" > "$LOG" 2>&1 &
PID=$!

XPID=$(pgrep -x Xorg | head -1)
t0=$SECONDS
prev=""
xprev=""
while kill -0 "$PID" 2>/dev/null && [ $((SECONDS - t0)) -lt "$DUR" ]; do
	u=$(awk '{u+=$14;s+=$15} END{print u+s}' /proc/$PID/task/*/stat 2>/dev/null)
	xu=""
	if [ -n "$XPID" ]; then
		xu=$(awk '{print $14+$15}' /proc/$XPID/stat 2>/dev/null)
	fi
	el=$((SECONDS - t0))
	if [ -n "$u" ] && [ -n "$prev" ] && [ "$el" -gt 0 ]; then
		cpu=$(awk -v tk="$((u - prev))" -v el="5" 'BEGIN{printf "%.0f", tk/el}')
		xcpu="?"
		if [ -n "$xu" ] && [ -n "$xprev" ]; then
			xcpu=$(awk -v tk="$((xu - xprev))" -v el="5" 'BEGIN{printf "%.0f", tk/el}')
		fi
		echo "t=${el}s pipe_cpu=$cpu xorg_cpu=$xcpu"
	fi
	prev="$u"
	xprev="$xu"
	sleep 5
done

kill -TERM "$PID" 2>/dev/null
wait "$PID" 2>/dev/null
echo "TREND_DONE"
