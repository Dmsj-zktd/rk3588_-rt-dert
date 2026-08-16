#!/bin/bash
# ============================================================================
# measure_run.sh - 板端性能采样脚本（任务 7）
# 用法: bash tests/measure_run.sh <log_name> <rtdetr_pipeline 参数...>
# 在 build 目录运行 rtdetr_pipeline，采样 ~50s 后发 SIGTERM 优雅退出，
# 输出 CPU%（全线程 utime+stime 增量 / 墙钟，USER_HZ=100）与峰值 RSS（VmHWM, KB）。
# 模型路径固定为板端默认模型，可通过环境变量 MODEL_PATH 覆盖。
# ============================================================================
set -u

LOG="$1"
shift

MODEL="${MODEL_PATH:-/home/neardi/Workspace_Codex/models/RT-DETR-RK3588-Models/.rknn/rtdetr_i8.rknn}"
BUILD_DIR="${PROJ_BUILD:-/home/neardi/Workspace_Codex/rk3588_-rt-detr/cpp_DMSJ/build}"
cd "$BUILD_DIR" || exit 2

./rtdetr_pipeline -m "$MODEL" "$@" > "$LOG" 2>&1 &
PID=$!

peak=0
u0=$(awk '{u+=$14;s+=$15} END{print u+s}' /proc/$PID/task/*/stat 2>/dev/null)
t0=$SECONDS
u1=$u0
while kill -0 "$PID" 2>/dev/null && [ $((SECONDS - t0)) -lt 50 ]; do
	u1=$(awk '{u+=$14;s+=$15} END{print u+s}' /proc/$PID/task/*/stat 2>/dev/null)
	rss=$(awk '/VmHWM/{print $2}' /proc/$PID/status 2>/dev/null)
	if [ -n "$rss" ] && [ "$rss" -gt "$peak" ]; then
		peak=$rss
	fi
	sleep 1
done

t1=$SECONDS
kill -TERM "$PID" 2>/dev/null
wait "$PID" 2>/dev/null

elapsed=$((t1 - t0))
ticks=0
if [ -n "$u1" ] && [ -n "$u0" ]; then
	ticks=$((u1 - u0))
fi
cpu=$(awk -v tk="$ticks" -v el="$elapsed" 'BEGIN{printf "%.1f\n", tk/el}')

echo "SAMPLES sec=$elapsed cpu_pct=$cpu peak_rss_kb=$peak"
grep -e Negotiated -e "Frames completed" -e "Overall FPS" -e "Avg preprocess" -e "Avg NPU" -e "Avg postprocess" "$LOG"
