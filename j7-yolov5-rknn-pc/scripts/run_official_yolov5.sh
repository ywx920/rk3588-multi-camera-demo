#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WORK_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
EXAMPLE_DIR="${WORK_DIR}/third_party/rknn-toolkit2/examples/onnx/yolov5"

cd "${EXAMPLE_DIR}"
"${WORK_DIR}/.venv/bin/python" test.py
