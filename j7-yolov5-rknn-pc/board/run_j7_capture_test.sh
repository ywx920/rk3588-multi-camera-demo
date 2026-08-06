#!/usr/bin/env bash
set -euo pipefail

CAMERA=${CAMERA:-/dev/video-camera0}
FRAMES=${FRAMES:-90}

echo "Camera: $CAMERA"
fuser -v "$CAMERA" 2>/dev/null || true
v4l2-ctl -d "$CAMERA" \
    --set-fmt-video=width=2112,height=1568,pixelformat=NV12 \
    --stream-mmap=4 --stream-count="$FRAMES" --stream-poll

