#!/usr/bin/env bash
set -euo pipefail
project="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
binary="${BINARY:-$project/build-board/zx-camera_qt_demo}"
[ -x "$binary" ] || { echo "ERROR: run build_board.sh first" >&2; exit 1; }
[ -n "${DISPLAY:-}" ] || { echo "ERROR: run from the board desktop terminal" >&2; exit 1; }
extra_args=()
if [ "${AUTO_START:-0}" = "1" ]; then
    extra_args+=(--auto-start)
fi
exec "$binary" --source all --inference \
    --display-fps "${DISPLAY_FPS:-15}" \
    --inference-fps "${INFERENCE_FPS:-8}" \
    --work-width "${WORK_WIDTH:-960}" \
    --npu-workers "${NPU_WORKERS:-3}" \
    --model /home/zxwy/j7-yolov5/models/yolov5s_relu_rk3588_int8.rknn \
    "${extra_args[@]}"
