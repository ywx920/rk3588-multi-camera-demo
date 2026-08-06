#!/usr/bin/env bash
set -euo pipefail
PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
command -v c++ >/dev/null || { echo "ERROR: c++ is missing"; exit 1; }
make -C "$PROJECT_DIR" clean all
"$PROJECT_DIR/zx-camera_web" --help
