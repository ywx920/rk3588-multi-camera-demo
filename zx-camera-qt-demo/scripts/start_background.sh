#!/usr/bin/env bash
set -euo pipefail

project="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
pid_file="$project/qt-camera.pid"
log_file="$project/qt-camera.log"

if [[ -f "$pid_file" ]] && kill -0 "$(<"$pid_file")" 2>/dev/null; then
    echo "Qt demo already running: PID $(<"$pid_file")"
    exit 0
fi

if pgrep -f '/zx-camera-web-demo/zx-camera_web' >/dev/null 2>&1; then
    echo "ERROR: Web demo is using the cameras; stop it first." >&2
    echo "Run: /home/zxwy/stop_camera_demos.sh" >&2
    exit 1
fi

export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/run/user/$(id -u)}"
desktop_pid="$(pgrep -u "$(id -u)" -x gnome-shell | head -n 1 || true)"
if [[ -n "$desktop_pid" && -r "/proc/$desktop_pid/environ" ]]; then
    desktop_display="$(tr '\0' '\n' <"/proc/$desktop_pid/environ" |
        sed -n 's/^DISPLAY=//p' | head -n 1)"
    desktop_authority="$(tr '\0' '\n' <"/proc/$desktop_pid/environ" |
        sed -n 's/^XAUTHORITY=//p' | head -n 1)"
    desktop_bus="$(tr '\0' '\n' <"/proc/$desktop_pid/environ" |
        sed -n 's/^DBUS_SESSION_BUS_ADDRESS=//p' | head -n 1)"
else
    desktop_display=""
    desktop_authority=""
    desktop_bus=""
fi
if [[ -z "$desktop_pid" || -z "$desktop_display" ]]; then
    echo "ERROR: no logged-in GNOME desktop session was found." >&2
    echo "Log in to the board desktop first, then run this script again." >&2
    exit 1
fi
export DISPLAY="${DISPLAY:-${desktop_display:-:0}}"
export XAUTHORITY="${XAUTHORITY:-${desktop_authority:-$HOME/.Xauthority}}"
export DBUS_SESSION_BUS_ADDRESS="${DBUS_SESSION_BUS_ADDRESS:-${desktop_bus:-unix:path=$XDG_RUNTIME_DIR/bus}}"

AUTO_START="${AUTO_START:-1}" nohup "$project/scripts/run_board_all.sh" \
    >"$log_file" 2>&1 </dev/null &
pid=$!
echo "$pid" >"$pid_file"
sleep 2
if ! kill -0 "$pid" 2>/dev/null; then
    cat "$log_file"
    rm -f "$pid_file"
    exit 1
fi

echo "Qt demo started in background: PID $pid"
echo "Log: $log_file"
