#!/usr/bin/env bash
set -euo pipefail

APP_DIR=${APP_DIR:-/home/zxwy/j7-yolov5}
cd "$APP_DIR"

echo "Using board system RKNN runtime:"
ldconfig -p | grep librknnrt.so || true
echo "Model checksum:"
sha256sum models/yolov5s_relu_rk3588_int8.rknn

rm -f out.bmp
./bin/yolov5_rknn \
    yolov5 q8 \
    models/yolov5s_relu_rk3588_int8.rknn \
    images/bus.jpg \
    2>&1 | tee logs/static-test.log

test -s out.bmp
echo "[PASS] Result image: $APP_DIR/out.bmp"

