#!/usr/bin/env bash
set -u

SCRIPT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
BINARY="${BINARY:-$PROJECT_DIR/build/zx-camera_minimal}"
CONFIG="${CONFIG:-$PROJECT_DIR/config/camera.conf}"

if [ ! -x "$BINARY" ]; then
    echo "ERROR: executable not found: $BINARY" >&2
    echo "Build the project first; see README.md." >&2
    exit 1
fi

exec "$BINARY" --config "$CONFIG" "$@"

