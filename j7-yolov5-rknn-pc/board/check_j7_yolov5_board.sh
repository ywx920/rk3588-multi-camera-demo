#!/usr/bin/env bash

set -u

EXPECTED_MODEL_SHA256="75f069f32269ef93afc23cbdc0410254e06c337f6be825743b393b568e8c08fd"
DEFAULT_MODEL="/home/zxwy/j7-yolov5/models/yolov5s_relu_rk3588_int8.rknn"
MODEL_PATH="${J7_RKNN_MODEL:-$DEFAULT_MODEL}"
CAMERA_DEVICE="${J7_CAMERA_DEVICE:-/dev/video-camera0}"
RUN_STREAM_TEST=0
PASS_COUNT=0
WARN_COUNT=0
FAIL_COUNT=0

if [[ "${1:-}" == "--stream-test" ]]; then
    RUN_STREAM_TEST=1
elif [[ $# -gt 0 ]]; then
    echo "Usage: $0 [--stream-test]"
    exit 2
fi

section() {
    printf '\n===== %s =====\n' "$1"
}

pass() {
    PASS_COUNT=$((PASS_COUNT + 1))
    printf '[PASS] %s\n' "$*"
}

warn() {
    WARN_COUNT=$((WARN_COUNT + 1))
    printf '[WARN] %s\n' "$*"
}

fail() {
    FAIL_COUNT=$((FAIL_COUNT + 1))
    printf '[FAIL] %s\n' "$*"
}

have() {
    command -v "$1" >/dev/null 2>&1
}

library_path() {
    ldconfig -p 2>/dev/null | awk -v name="$1" '$1 == name { print $NF; exit }'
}

section "Basic system"
echo "time: $(date -Is 2>/dev/null || date)"
echo "host: $(hostname)"
echo "kernel: $(uname -srmo)"
echo "user: $(id)"

if [[ "$(uname -m)" == "aarch64" ]]; then
    pass "Architecture is aarch64"
else
    fail "Architecture is $(uname -m), expected aarch64"
fi

if getent group video | grep -Eq "(^|,)${USER}(,|$)" || id -nG | tr ' ' '\n' | grep -qx video; then
    pass "Current user belongs to video group"
else
    warn "Current user is not in video group"
fi

if getent group render | grep -Eq "(^|,)${USER}(,|$)" || id -nG | tr ' ' '\n' | grep -qx render; then
    pass "Current user belongs to render group"
else
    warn "Current user is not in render group; sudo may be required for NPU"
fi

section "DRM and RKNPU"
if [[ -d /dev/dri ]]; then
    ls -l /dev/dri
else
    fail "/dev/dri is missing"
fi

DRM_COUNT=$(find /dev/dri -maxdepth 1 -type c 2>/dev/null | wc -l)
if (( DRM_COUNT > 0 )); then
    pass "Found ${DRM_COUNT} DRM device node(s)"
else
    fail "No DRM character device found"
fi

for node in /sys/class/drm/card* /sys/class/drm/renderD*; do
    [[ -e "$node" ]] || continue
    echo "--- $node"
    echo "device: $(readlink -f "$node/device" 2>/dev/null || true)"
    echo "driver: $(readlink -f "$node/device/driver" 2>/dev/null || true)"
done

RKNPU_LOG=$(dmesg 2>/dev/null | grep -i rknpu || true)
if [[ -n "$RKNPU_LOG" ]]; then
    echo "$RKNPU_LOG"
else
    warn "Cannot read RKNPU dmesg or no RKNPU log found"
fi

if grep -q 'Initialized rknpu' <<<"$RKNPU_LOG"; then
    pass "RKNPU driver initialized"
else
    fail "RKNPU initialization line not found"
fi

if grep -q 'iommu is enabled' <<<"$RKNPU_LOG"; then
    pass "RKNPU IOMMU is enabled"
else
    warn "RKNPU IOMMU enabled line not found"
fi

# Do not use a loose `iommu.*fault` expression here: the normal message
# "Default domain type" contains the substring "fault".
ERROR_LOG=$(dmesg 2>/dev/null | grep -iE \
    'rknpu.*(job timeout|reset|page fault|unhandled fault)|iommu.*(page fault|fault at|unhandled fault)' \
    || true)
if [[ -z "$ERROR_LOG" ]]; then
    pass "No RKNPU/IOMMU fault, reset or timeout in current dmesg"
else
    fail "RKNPU/IOMMU error found"
    echo "$ERROR_LOG"
fi

POWER_WARN=$(dmesg 2>/dev/null | grep -iE 'rknpu.*(power_model|dynamic-coefficient)' || true)
if [[ -n "$POWER_WARN" ]]; then
    warn "NPU power-model data is incomplete; functional test may still work"
    echo "$POWER_WARN"
fi

section "RKNN and RGA runtime"
RKNN_LIB=$(library_path librknnrt.so)
RGA_LIB=$(library_path librga.so)

if [[ -n "$RKNN_LIB" && -e "$RKNN_LIB" ]]; then
    pass "librknnrt.so: $RKNN_LIB"
    ls -l "$RKNN_LIB"
    sha256sum "$RKNN_LIB" 2>/dev/null || true
else
    fail "librknnrt.so not found by ldconfig"
fi

if [[ -n "$RGA_LIB" && -e "$RGA_LIB" ]]; then
    pass "librga.so: $RGA_LIB"
    ls -l "$RGA_LIB"
else
    fail "librga.so not found by ldconfig"
fi

for header in \
    /usr/include/rknn_api.h \
    /usr/include/rknn/rknn_api.h \
    /usr/local/include/rknn_api.h \
    /usr/include/rga/im2d.h \
    /usr/include/rga/RgaApi.h; do
    [[ -f "$header" ]] && echo "header: $header"
done

if find /usr/include /usr/local/include -type f -name rknn_api.h 2>/dev/null | grep -q .; then
    pass "rknn_api.h is installed (board-side compilation available)"
else
    warn "rknn_api.h is missing; prebuilt AArch64 runner can still run"
fi

if [[ -f /usr/include/rga/im2d.h && -f /usr/include/rga/RgaApi.h ]]; then
    pass "RGA development headers are installed"
else
    warn "RGA development headers are incomplete"
fi

section "Model"
echo "model path: $MODEL_PATH"
if [[ -f "$MODEL_PATH" ]]; then
    MODEL_SHA256=$(sha256sum "$MODEL_PATH" | awk '{print $1}')
    echo "model size: $(stat -c %s "$MODEL_PATH") bytes"
    echo "model sha256: $MODEL_SHA256"
    if [[ "$MODEL_SHA256" == "$EXPECTED_MODEL_SHA256" ]]; then
        pass "RKNN model hash matches the PC-generated model"
    else
        fail "RKNN model hash mismatch"
    fi
else
    warn "Model not deployed yet: $MODEL_PATH"
fi

section "J7 camera"
if [[ -e "$CAMERA_DEVICE" ]]; then
    pass "Camera node exists: $CAMERA_DEVICE"
    ls -l "$CAMERA_DEVICE"
else
    fail "Camera node is missing: $CAMERA_DEVICE"
fi

if [[ -e "$CAMERA_DEVICE" ]] && have udevadm; then
    CAMERA_PROPS=$(udevadm info -q property -n "$CAMERA_DEVICE" 2>/dev/null || true)
    echo "$CAMERA_PROPS" | grep -E 'DEVNAME=|ID_V4L_PRODUCT=|ID_PATH=' || true
    if grep -q 'ID_V4L_PRODUCT=rkisp_mainpath' <<<"$CAMERA_PROPS"; then
        pass "J7 alias points to rkisp_mainpath"
    else
        warn "Cannot confirm rkisp_mainpath from udev properties"
    fi
fi

if [[ -e "$CAMERA_DEVICE" ]] && have fuser; then
    CAMERA_USERS=$(fuser "$CAMERA_DEVICE" 2>/dev/null || true)
    if [[ -z "$CAMERA_USERS" ]]; then
        pass "J7 mainpath is not occupied"
    else
        fail "J7 mainpath is occupied by PID(s): $CAMERA_USERS"
        fuser -v "$CAMERA_DEVICE" 2>/dev/null || true
    fi
fi

if [[ -e "$CAMERA_DEVICE" ]] && have v4l2-ctl; then
    v4l2-ctl -D -d "$CAMERA_DEVICE" 2>/dev/null | sed -n '1,28p'
    FORMAT_INFO=$(v4l2-ctl -d "$CAMERA_DEVICE" --get-fmt-video 2>/dev/null || true)
    echo "$FORMAT_INFO"
    if grep -q "Pixel Format.*'NV12'" <<<"$FORMAT_INFO"; then
        pass "J7 current mainpath format is NV12"
    else
        warn "J7 current format is not NV12; application can set it before streaming"
    fi
else
    warn "v4l2-ctl unavailable or camera missing"
fi

if (( RUN_STREAM_TEST )); then
    section "Optional J7 90-frame stream test"
    if [[ ! -e "$CAMERA_DEVICE" ]]; then
        fail "Cannot stream: camera node missing"
    elif ! have v4l2-ctl; then
        fail "Cannot stream: v4l2-ctl missing"
    elif [[ -n "$(fuser "$CAMERA_DEVICE" 2>/dev/null || true)" ]]; then
        fail "Cannot stream: camera is occupied"
    else
        STREAM_LOG=$(mktemp /tmp/j7-stream-check.XXXXXX)
        if v4l2-ctl -d "$CAMERA_DEVICE" \
            --set-fmt-video=width=2112,height=1568,pixelformat=NV12 \
            --stream-mmap=4 --stream-count=90 --stream-poll \
            >"$STREAM_LOG" 2>&1 && \
            ! grep -qiE 'select timeout|VIDIOC_.*failed|Device or resource busy' "$STREAM_LOG"; then
            cat "$STREAM_LOG"
            pass "J7 90-frame stream test completed"
        else
            cat "$STREAM_LOG"
            fail "J7 stream test failed"
        fi
        command -v shred >/dev/null 2>&1 && shred -u "$STREAM_LOG" || true
    fi
else
    echo "Stream test skipped. Run with --stream-test to capture 90 frames."
fi

section "Runner readiness"
RUNNER="/home/zxwy/j7-yolov5/bin/yolov5_rknn"
if [[ -x "$RUNNER" ]]; then
    pass "AArch64 YOLOv5 runner exists: $RUNNER"
    file "$RUNNER" 2>/dev/null || true
    MISSING_LIBS=$(ldd "$RUNNER" 2>/dev/null | grep 'not found' || true)
    if [[ -z "$MISSING_LIBS" ]]; then
        pass "Runner has no missing dynamic library"
    else
        fail "Runner has missing libraries"
        echo "$MISSING_LIBS"
    fi
else
    warn "YOLOv5 runner has not been deployed yet: $RUNNER"
fi

section "Summary"
echo "PASS=$PASS_COUNT WARN=$WARN_COUNT FAIL=$FAIL_COUNT"
if (( FAIL_COUNT > 0 )); then
    echo "RESULT=NOT_READY"
    exit 1
fi

if (( WARN_COUNT > 0 )); then
    echo "RESULT=READY_WITH_WARNINGS"
else
    echo "RESULT=READY"
fi
