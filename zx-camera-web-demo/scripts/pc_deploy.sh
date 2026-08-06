#!/usr/bin/env bash
set -euo pipefail
BOARD="${BOARD:-zxwy@192.168.10.65}"
REMOTE_DIR="${REMOTE_DIR:-/home/zxwy/zx-camera-web-demo}"
PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ARCHIVE="$(mktemp /tmp/zx-camera-web-demo.XXXXXX.tar.gz)"
trap 'rm -f "$ARCHIVE"' EXIT
tar -C "$(dirname "$PROJECT_DIR")" \
    --exclude='zx-camera-web-demo/zx-camera_web' \
    -czf "$ARCHIVE" "$(basename "$PROJECT_DIR")"
scp "$ARCHIVE" "$BOARD:/tmp/zx-camera-web-demo.tar.gz"
ssh "$BOARD" "mkdir -p '$REMOTE_DIR' && tar -xzf /tmp/zx-camera-web-demo.tar.gz \
    --strip-components=1 -C '$REMOTE_DIR' && rm -f /tmp/zx-camera-web-demo.tar.gz \
    && chmod +x '$REMOTE_DIR'/scripts/*.sh"
echo "Deployed to $BOARD:$REMOTE_DIR"
