#!/usr/bin/env bash
set -u
set -o pipefail

NODE="${1:-/dev/video22}"
COUNT="${STREAM_COUNT:-30}"
LIMIT="${STREAM_TIMEOUT:-30}"
OUT="${2:-/tmp/zx-camera-precheck-$(date +%Y%m%d-%H%M%S).txt}"

{
    echo "===== system ====="
    uname -a
    echo "node=$NODE"
    echo
    echo "===== tools ====="
    command -v cmake || true
    command -v c++ || true
    command -v v4l2-ctl || true
    echo
    echo "===== node name ====="
    base="$(basename "$NODE")"
    cat "/sys/class/video4linux/$base/name" 2>&1 || true

    if ! command -v v4l2-ctl >/dev/null 2>&1; then
        echo "SKIP: v4l2-ctl not found"
        exit 127
    fi

    echo
    echo "===== capabilities ====="
    v4l2-ctl -d "$NODE" --all
    echo
    echo "===== formats ====="
    v4l2-ctl -d "$NODE" --list-formats-ext
    echo
    echo "===== bounded stream ====="
    timeout --signal=INT --kill-after=5s "${LIMIT}s" \
        v4l2-ctl -d "$NODE" --stream-mmap=4 \
        --stream-count="$COUNT" --stream-to=/dev/null --verbose
} 2>&1 | tee "$OUT"

exit "${PIPESTATUS[0]}"

