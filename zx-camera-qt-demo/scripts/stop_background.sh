#!/usr/bin/env bash
set -euo pipefail

project="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
pid_file="$project/qt-camera.pid"

if [[ ! -f "$pid_file" ]]; then
    echo "Qt demo is not running (PID file absent)"
    exit 0
fi

pid="$(<"$pid_file")"
if kill -0 "$pid" 2>/dev/null; then
    kill -TERM "$pid"
    for _ in {1..50}; do
        kill -0 "$pid" 2>/dev/null || break
        sleep 0.1
    done
fi
if kill -0 "$pid" 2>/dev/null; then
    echo "ERROR: Qt process $pid did not stop" >&2
    exit 1
fi
rm -f "$pid_file"
echo "Qt demo stopped safely"
