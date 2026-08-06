#!/usr/bin/env bash
set -euo pipefail

PROJECT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
BINARY="$PROJECT_DIR/build/zx-camera_qt_demo"

if [ ! -x "$BINARY" ]; then
    echo "ERROR: build first with: bash scripts/build_pc.sh" >&2
    exit 1
fi

exec "$BINARY" "$@"

