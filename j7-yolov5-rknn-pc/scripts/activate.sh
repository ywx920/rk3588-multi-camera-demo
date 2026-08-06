#!/usr/bin/env bash

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WORK_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

source "${WORK_DIR}/.venv/bin/activate"
export J7_RKNN_PC_WORKDIR="${WORK_DIR}"
echo "RKNN PC environment: ${WORK_DIR}"
python --version

