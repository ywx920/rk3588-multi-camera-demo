#!/usr/bin/env bash
set -euo pipefail

home_dir="${DEMO_HOME:-/home/zxwy}"
for script in \
    "$home_dir/zx-camera-web-demo/scripts/stop_background.sh" \
    "$home_dir/zx-camera-qt-demo/scripts/stop_background.sh"
do
    if [[ -x "$script" ]]; then
        "$script"
    fi
done

echo "Camera demo shutdown complete"
