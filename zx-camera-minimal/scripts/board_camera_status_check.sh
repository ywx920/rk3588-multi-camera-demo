#!/usr/bin/env bash
#
# Read-only RKAIQ/camera health check. It does not replace IQ files or restart
# services. A short V4L2 capture is used to verify the complete data path.

set -u
set -o pipefail

TOOL_ROOT="${TOOL_ROOT:-/home/zxwy/zx-camera-tools}"
CAMERA_BIN="${CAMERA_BIN:-$TOOL_ROOT/bin/zx-camera_minimal}"
TARGET_IQ="${TARGET_IQ:-/etc/iqfiles/ov13850_ZC-OV13850R2A-V1_Largan-50064B31.json}"
EXPECTED_IQ_SHA256="${EXPECTED_IQ_SHA256:-17477115021e7f2c5c7632ab3af1df5c141952935547a45de6f7905d66b554c2}"
DEVICE="${DEVICE:-/dev/video22}"
WIDTH="${WIDTH:-2112}"
HEIGHT="${HEIGHT:-1568}"
STAMP="$(date +%Y%m%d-%H%M%S)"
CHECK_START="$(date '+%Y-%m-%d %H:%M:%S')"
REPORT_DIR="${REPORT_DIR:-$TOOL_ROOT/reports/zx-camera-check-$STAMP}"
FAILED=0

mkdir -p "$REPORT_DIR" || exit 1
exec > >(tee -a "$REPORT_DIR/check.log") 2>&1

section()
{
    printf '\n===== %s =====\n' "$1"
}

fail()
{
    echo "FAIL: $*"
    FAILED=1
}

section "Files and configuration"
for path in "$TARGET_IQ" "$CAMERA_BIN" "$DEVICE"; do
    if [ ! -e "$path" ]; then
        fail "required path not found: $path"
    fi
done

if [ -f "$TARGET_IQ" ]; then
    ACTUAL_SHA256="$(sha256sum "$TARGET_IQ" | awk '{print $1}')"
    echo "IQ file       : $TARGET_IQ"
    echo "IQ SHA256     : $ACTUAL_SHA256"
    echo "Expected SHA  : $EXPECTED_IQ_SHA256"
    [ "$ACTUAL_SHA256" = "$EXPECTED_IQ_SHA256" ] ||
        fail "installed IQ is not the validated v3 file"
    grep -o '"scene_isp[0-9]*"' "$TARGET_IQ" | head -n 1 || true
fi

section "RKAIQ service"
systemctl status rkaiq_3A.service --no-pager -l || fail "RKAIQ service is not active"
pgrep -a rkaiq_3A_server || fail "rkaiq_3A_server is not running"

section "Camera node"
ls -l "$DEVICE" 2>/dev/null || true
if command -v v4l2-ctl >/dev/null 2>&1; then
    v4l2-ctl -d "$DEVICE" --get-fmt-video || fail "cannot read V4L2 format"
fi

if [ "$FAILED" -eq 0 ]; then
    section "Capture 60 frames"
    timeout --signal=INT --kill-after=3s 15s "$CAMERA_BIN" \
        --device "$DEVICE" --width "$WIDTH" --height "$HEIGHT" \
        --format NV12 --frames 60 --save-count 1 \
        --output "$REPORT_DIR/frames" --log-interval 30 \
        > "$REPORT_DIR/capture.log" 2>&1 ||
        fail "60-frame capture failed"
    cat "$REPORT_DIR/capture.log"

    CAPTURED="$(grep 'Total frames' "$REPORT_DIR/capture.log" |
        tail -n 1 | awk '{print $4}')"
    FPS="$(grep 'Average FPS' "$REPORT_DIR/capture.log" |
        tail -n 1 | awk '{print $4}')"
    GAPS="$(grep 'Sequence gaps' "$REPORT_DIR/capture.log" |
        tail -n 1 | awk '{print $4}')"
    [ "$CAPTURED" = "60" ] || fail "expected 60 frames, got ${CAPTURED:-missing}"
    # RKISP may discard one frame while 3A converges immediately after STREAMON.
    # More than one missing sequence in this short run is treated as unstable.
    awk -v gaps="${GAPS:-999}" 'BEGIN { exit !(gaps <= 1) }' ||
        fail "sequence gaps exceed startup allowance: ${GAPS:-missing}"
    awk -v fps="${FPS:-0}" 'BEGIN { exit !(fps >= 25.0) }' ||
        fail "average FPS below 25: ${FPS:-missing}"
fi

section "Recent RKAIQ errors"
journalctl -u rkaiq_3A.service --since "$CHECK_START" --no-pager 2>/dev/null |
    grep -E 'lost smartRun|Segmentation|core dumped|ALSC:E|AWB:E' |
    tee "$REPORT_DIR/recent-errors.log" || true
if [ -s "$REPORT_DIR/recent-errors.log" ]; then
    fail "recent fatal RKAIQ errors found"
fi

section "Result"
if [ "$FAILED" -eq 0 ]; then
    echo "result=PASS"
else
    echo "result=FAIL"
fi
echo "Report directory: $REPORT_DIR"
exit "$FAILED"
