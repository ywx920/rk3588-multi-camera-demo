#!/usr/bin/env bash
set -euo pipefail
PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PID_FILE="$PROJECT_DIR/web-preview.pid"

if [[ ! -f "$PID_FILE" ]]; then
    echo "Not running (PID file absent)"
    exit 0
fi
PID="$(cat "$PID_FILE")"
if kill -0 "$PID" 2>/dev/null; then
    kill -INT "$PID"
    for _ in {1..20}; do
        kill -0 "$PID" 2>/dev/null || break
        sleep 0.1
    done
fi
if kill -0 "$PID" 2>/dev/null; then
    echo "ERROR: process $PID did not stop"
    exit 1
fi
rm -f "$PID_FILE"
echo "Stopped safely"
