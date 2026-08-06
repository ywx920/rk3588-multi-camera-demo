#!/usr/bin/env bash
set -euo pipefail
project="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
pid_file="$project/stability-monitor.pid"
log_file="$project/stability-monitor.log"
if [[ -f "$pid_file" ]] && kill -0 "$(<"$pid_file")" 2>/dev/null; then
    echo "Monitor already running: PID $(<"$pid_file")"
    exit 0
fi
nohup "$project/scripts/monitor_stability.sh" >"$log_file" 2>&1 </dev/null &
echo "$!" >"$pid_file"
echo "Stability monitor started: PID $(<"$pid_file")"
echo "Log: $log_file"
