#!/usr/bin/env bash
set -euo pipefail
PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PID_FILE="$PROJECT_DIR/web-preview.pid"
LOG_FILE="$PROJECT_DIR/web-preview.log"

if [[ -f "$PID_FILE" ]] && kill -0 "$(cat "$PID_FILE")" 2>/dev/null; then
    echo "Already running: PID $(cat "$PID_FILE")"
    exit 0
fi

if pgrep -f '/zx-camera-qt-demo/.*/zx-camera_qt_demo' >/dev/null 2>&1; then
    echo "ERROR: Qt demo is using the cameras; stop it first." >&2
    echo "Run: /home/zxwy/stop_camera_demos.sh" >&2
    exit 1
fi

nohup "$PROJECT_DIR/scripts/run_uc325_web.sh" \
    >"$LOG_FILE" 2>&1 </dev/null &
PID=$!
echo "$PID" >"$PID_FILE"
sleep 1
if ! kill -0 "$PID" 2>/dev/null; then
    cat "$LOG_FILE"
    rm -f "$PID_FILE"
    exit 1
fi
echo "Started: PID $PID"
echo "URL: http://${WEB_BIND_ADDRESS:-127.0.0.1}:${PORT:-8080}/"
