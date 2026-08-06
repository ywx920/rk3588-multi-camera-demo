#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WORK_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
PYTHON="${WORK_DIR}/.venv/bin/python"

"${PYTHON}" - <<'PY'
from rknn.api import RKNN
import cv2
import numpy
import onnx
import onnxruntime
import scipy
import torch
import torchvision

print('RKNN import: OK')
print('numpy:', numpy.__version__)
print('onnx:', onnx.__version__)
print('onnxruntime:', onnxruntime.__version__)
print('opencv:', cv2.__version__)
print('scipy:', scipy.__version__)
print('torch:', torch.__version__)
print('torchvision:', torchvision.__version__)
print('CUDA available:', torch.cuda.is_available())
rknn = RKNN(verbose=False)
rknn.release()
print('RKNN object: OK')
PY
