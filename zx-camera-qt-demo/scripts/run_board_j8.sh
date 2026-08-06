#!/usr/bin/env bash
set -euo pipefail
project="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
binary="${BINARY:-$project/build-board/zx-camera_qt_demo}"
device="${DEVICE:-/dev/video-camera1}"
[ -x "$binary" ] || { echo "ERROR: run build_board.sh first" >&2; exit 1; }
[ -n "${DISPLAY:-}" ] ||
    { echo "ERROR: DISPLAY is empty; run from board desktop terminal" >&2; exit 1; }
exec "$binary" --source ov13850-j8 --device "$device" \
    --width 2112 --height 1568 --format NV12 --fps 30 \
    --display-fps "${DISPLAY_FPS:-15}" \
    --inference-fps "${INFERENCE_FPS:-8}" \
    --work-width "${WORK_WIDTH:-960}" \
    --inference \
    --model /home/zxwy/j7-yolov5/models/yolov5s_relu_rk3588_int8.rknn
