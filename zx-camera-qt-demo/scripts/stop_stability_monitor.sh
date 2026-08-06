#!/usr/bin/env bash
set -euo pipefail
project="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
pid_file="$project/stability-monitor.pid"
if [[ ! -f "$pid_file" ]]; then
    echo "Monitor is not running"
    exit 0
fi
pid="$(<"$pid_file")"
if kill -0 "$pid" 2>/dev/null; then kill -TERM "$pid"; fi
rm -f "$pid_file"
echo "Stability monitor stopped"
