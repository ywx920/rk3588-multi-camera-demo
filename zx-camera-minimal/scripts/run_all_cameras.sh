#!/usr/bin/env bash
set -euo pipefail

project="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
binary="${BINARY:-$project/build/zx-camera_minimal}"
frames="${FRAMES:-300}"
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

[ -x "$binary" ] || { echo "ERROR: binary not found: $binary" >&2; exit 1; }

pids=()
names=()
start_camera()
{
    local name="$1" device="$2" width="$3" height="$4" format="$5"
    "$binary" --device "$device" --width "$width" --height "$height" \
        --format "$format" --frames "$frames" --save-count 0 \
        --output "/tmp/zx-camera-$name" \
        >"/tmp/zx-camera-$name.log" 2>&1 &
    pids+=("$!")
    names+=("$name")
}

start_camera j7 /dev/video-camera0 2112 1568 NV12
start_camera j8 /dev/video-camera1 2112 1568 NV12
start_camera uc325 "$uc325" 1280 720 MJPG

result=0
for index in "${!pids[@]}"; do
    if wait "${pids[$index]}"; then
        echo "${names[$index]}: PASS"
    else
        echo "${names[$index]}: FAIL (see /tmp/zx-camera-${names[$index]}.log)" >&2
        result=1
    fi
done
exit "$result"
