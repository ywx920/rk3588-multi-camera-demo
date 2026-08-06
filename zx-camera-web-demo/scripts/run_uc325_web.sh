#!/usr/bin/env bash
set -euo pipefail
PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PORT="${PORT:-8080}"
uc325="${UC325_DEVICE:-}"
if [ -z "$uc325" ]; then
    for name in /sys/class/video4linux/video*/name; do
        if grep -q 'UC325' "$name"; then
            candidate="/dev/$(basename "$(dirname "$name")")"
            if v4l2-ctl -D -d "$candidate" 2>/dev/null | grep -q 'Video Capture'; then
                uc325="$candidate"
                break
            fi
        fi
    done
fi
[ -n "$uc325" ] || { echo "ERROR: UC325 capture node not found" >&2; exit 1; }
exec "$PROJECT_DIR/zx-camera_web" \
    --j7 "${J7_DEVICE:-/dev/video-camera0}" \
    --j8 "${J8_DEVICE:-/dev/video-camera1}" \
    --uc325 "$uc325" \
    --ov-encode-every "${OV_ENCODE_EVERY:-3}" \
    --port "$PORT"
