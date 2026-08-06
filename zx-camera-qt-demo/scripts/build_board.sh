#!/usr/bin/env bash
set -euo pipefail
project="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
build="${BUILD_DIR:-$project/build-board}"
for tool in cmake c++; do
    command -v "$tool" >/dev/null 2>&1 ||
        { echo "ERROR: missing $tool; run check_board_env.sh" >&2; exit 1; }
done
cmake -S "$project" -B "$build" -DCMAKE_BUILD_TYPE=Release
cmake --build "$build" -j"$(nproc)"
QT_QPA_PLATFORM=offscreen "$build/zx-camera_qt_demo" --help
