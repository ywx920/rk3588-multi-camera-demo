#!/usr/bin/env bash
#
# OV13850 IQ compatibility test for RK3588 ISP HW 30.
# Run on board:
#   sudo bash /home/zxwy/zx-camera-tools/scripts/board_iq_compat_test.sh
#
# The selected IQ file is temporarily replaced with an ISP30 OV13850 file.
# A trap restores the original file on normal exit, error, SIGINT and SIGTERM.

set -u
set -o pipefail

TARGET_IQ="/etc/iqfiles/ov13850_ZC-OV13850R2A-V1_Largan-50064B31.json"
TOOL_ROOT="${TOOL_ROOT:-/home/zxwy/zx-camera-tools}"
CANDIDATE_IQ="${CANDIDATE_IQ:-$TOOL_ROOT/iq/ov13850_ZC-isp30-compat-v3.json}"
CAMERA_BIN="${CAMERA_BIN:-$TOOL_ROOT/bin/zx-camera_minimal}"
DEVICE="${DEVICE:-/dev/video22}"
WIDTH="${WIDTH:-2112}"
HEIGHT="${HEIGHT:-1568}"
STAMP="$(date +%Y%m%d-%H%M%S)"
REPORT_ROOT="${REPORT_ROOT:-$TOOL_ROOT/reports}"
REPORT_DIR="$REPORT_ROOT/zx-iq-compat-$STAMP"
BACKUP_IQ="$REPORT_DIR/$(basename "$TARGET_IQ").backup"
AIQ_PID=""
RESTORED=0
FAILED=0
INSTALL_ON_PASS=0
PERMANENT_BACKUP=""

case "${1:-}" in
    "") ;;
    --install-on-pass) INSTALL_ON_PASS=1 ;;
    *)
        echo "Usage: $0 [--install-on-pass]" >&2
        exit 2
        ;;
esac

mkdir -p "$REPORT_DIR" || exit 1
exec > >(tee -a "$REPORT_DIR/test.log") 2>&1

section()
{
    printf '\n===== %s =====\n' "$1"
}

fail()
{
    echo "FAIL: $*"
    FAILED=1
}

restore()
{
    if [ -n "$AIQ_PID" ] && kill -0 "$AIQ_PID" 2>/dev/null; then
        kill "$AIQ_PID" 2>/dev/null || true
        wait "$AIQ_PID" 2>/dev/null || true
    fi
    killall rkaiq_3A_server 2>/dev/null || true

    if [ "$RESTORED" -eq 0 ] && [ -f "$BACKUP_IQ" ]; then
        cp -p "$BACKUP_IQ" "$TARGET_IQ" || {
            echo "CRITICAL: failed to restore $TARGET_IQ" >&2
            return
        }
        sync
        RESTORED=1
        echo "Original IQ restored: $TARGET_IQ"
    fi
}

finish()
{
    restore
}

trap finish EXIT
trap 'finish; exit 130' INT TERM

section "Preflight"
if [ "$(id -u)" -ne 0 ]; then
    echo "ERROR: run with sudo: sudo bash $0" >&2
    exit 1
fi

for path in "$TARGET_IQ" "$CANDIDATE_IQ" "$CAMERA_BIN"; do
    if [ ! -f "$path" ]; then
        echo "ERROR: required file not found: $path" >&2
        exit 1
    fi
done
if [ ! -x "$CAMERA_BIN" ]; then
    echo "ERROR: camera binary is not executable: $CAMERA_BIN" >&2
    exit 1
fi

echo "Target IQ   : $TARGET_IQ"
echo "Candidate IQ: $CANDIDATE_IQ"
echo "Camera      : $CAMERA_BIN"
sha256sum "$TARGET_IQ" "$CANDIDATE_IQ" |
    tee "$REPORT_DIR/iq-checksums-before.txt"
grep -o '"scene_isp[0-9]*"' "$TARGET_IQ" | head -n 1 || true
grep -o '"scene_isp[0-9]*"' "$CANDIDATE_IQ" | head -n 1 || true

section "Stop existing AIQ"
systemctl stop rkaiq_3A.service 2>&1 || true
killall rkaiq_3A_server 2>/dev/null || true
sleep 1

section "Install temporary ISP30 IQ"
cp -p "$TARGET_IQ" "$BACKUP_IQ" || exit 1
cp -p "$CANDIDATE_IQ" "$TARGET_IQ" || exit 1
sync
echo "Temporary override installed"
sha256sum "$TARGET_IQ" "$BACKUP_IQ" |
    tee "$REPORT_DIR/iq-checksums-active.txt"

section "Start RKAIQ directly"
ulimit -c 0
/usr/bin/rkaiq_3A_server > "$REPORT_DIR/rkaiq.log" 2>&1 &
AIQ_PID=$!
echo "RKAIQ PID=$AIQ_PID"
sleep 3

if ! kill -0 "$AIQ_PID" 2>/dev/null; then
    fail "RKAIQ exited during startup"
    cat "$REPORT_DIR/rkaiq.log"
else
    echo "RKAIQ is running after 3 seconds"
fi

if [ "$FAILED" -eq 0 ]; then
    section "Single-stream warmup and comparison capture"
    timeout --signal=INT --kill-after=3s 25s "$CAMERA_BIN" \
        --device "$DEVICE" --width "$WIDTH" --height "$HEIGHT" \
        --format NV12 --frames 180 --save-count 3 --save-start 121 \
        --output "$REPORT_DIR/frames" --log-interval 60 \
        > "$REPORT_DIR/capture.log" 2>&1 ||
        fail "comparison capture failed"
    tail -n 40 "$REPORT_DIR/capture.log" || true
    find "$REPORT_DIR/frames" -maxdepth 1 -type f -name '*.raw' \
        -exec stat -c '%n %s bytes' {} \; 2>/dev/null || true

    CAPTURED="$(grep 'Total frames' "$REPORT_DIR/capture.log" |
        tail -n 1 | awk '{print $4}')"
    FPS="$(grep 'Average FPS' "$REPORT_DIR/capture.log" |
        tail -n 1 | awk '{print $4}')"
    FILE_COUNT="$(find "$REPORT_DIR/frames" -maxdepth 1 \
        -type f -name '*.raw' | wc -l)"
    echo "Validated frames=$CAPTURED fps=$FPS files=$FILE_COUNT"
    [ "$CAPTURED" = "180" ] ||
        fail "expected 180 captured frames, got ${CAPTURED:-missing}"
    [ "$FILE_COUNT" -eq 3 ] ||
        fail "expected 3 saved frames, got $FILE_COUNT"
    awk -v fps="$FPS" 'BEGIN { exit !(fps >= 25.0) }' ||
        fail "average FPS is below 25: ${FPS:-missing}"
    kill -0 "$AIQ_PID" 2>/dev/null ||
        fail "RKAIQ exited during capture"
fi

section "Stop AIQ and restore original IQ"
restore
sha256sum "$TARGET_IQ" "$BACKUP_IQ" |
    tee "$REPORT_DIR/iq-checksums-restored.txt"
if ! cmp -s "$TARGET_IQ" "$BACKUP_IQ"; then
    fail "restored IQ differs from backup"
fi

section "Result"
grep -E 'lost smartRun|Segmentation|core dumped|ALSC:E|AWB:E|ERROR' \
    "$REPORT_DIR/rkaiq.log" > "$REPORT_DIR/rkaiq-errors.txt" || true
cat "$REPORT_DIR/rkaiq-errors.txt" 2>/dev/null || true
if [ -s "$REPORT_DIR/rkaiq-errors.txt" ]; then
    fail "RKAIQ reported fatal compatibility errors"
fi

if [ "$FAILED" -eq 0 ] && [ "$INSTALL_ON_PASS" -eq 1 ]; then
    section "Install validated compatibility IQ"
    PERMANENT_BACKUP="${TARGET_IQ}.pre-compat-${STAMP}.bak"
    cp -p "$TARGET_IQ" "$PERMANENT_BACKUP" ||
        fail "cannot create permanent IQ backup"
    if [ "$FAILED" -eq 0 ]; then
        cp -p "$CANDIDATE_IQ" "$TARGET_IQ" || fail "cannot install candidate IQ"
        sync
    fi
    if [ "$FAILED" -eq 0 ]; then
        systemctl restart rkaiq_3A.service || fail "cannot restart RKAIQ service"
        sleep 3
    fi
    if [ "$FAILED" -eq 0 ] && ! pgrep -x rkaiq_3A_server >/dev/null; then
        fail "RKAIQ service did not remain running after installation"
    fi
    if [ "$FAILED" -ne 0 ]; then
        systemctl stop rkaiq_3A.service 2>/dev/null || true
        if [ -f "$PERMANENT_BACKUP" ]; then
            cp -p "$PERMANENT_BACKUP" "$TARGET_IQ"
            sync
            echo "Permanent installation rolled back"
        fi
    else
        echo "Installed IQ: $TARGET_IQ"
        echo "Backup IQ   : $PERMANENT_BACKUP"
        pgrep -a rkaiq_3A_server
    fi
fi

if [ "$FAILED" -eq 0 ]; then
    if [ "$INSTALL_ON_PASS" -eq 1 ]; then
        echo "result=PASS_AND_INSTALLED" | tee "$REPORT_DIR/result.txt"
    else
        echo "result=PASS_ISP30_COMPAT_TEST" | tee "$REPORT_DIR/result.txt"
    fi
else
    echo "result=FAIL" | tee "$REPORT_DIR/result.txt"
    systemctl restart rkaiq_3A.service 2>/dev/null || true
fi

ARCHIVE="${REPORT_DIR}.tar.gz"
tar -czf "$ARCHIVE" -C "$(dirname "$REPORT_DIR")" \
    "$(basename "$REPORT_DIR")"
echo "Report archive: $ARCHIVE"
REPORT_OWNER="${SUDO_USER:-zxwy}"
if id "$REPORT_OWNER" >/dev/null 2>&1; then
    chown -R "$REPORT_OWNER":"$(id -gn "$REPORT_OWNER")" \
        "$REPORT_DIR" "$ARCHIVE" 2>/dev/null || true
fi
if [ "$INSTALL_ON_PASS" -eq 1 ] && [ "$FAILED" -eq 0 ]; then
    echo "Validated IQ is installed and rkaiq_3A.service is running."
else
    echo "Previous IQ is restored and rkaiq_3A.service was restarted."
fi

trap - EXIT INT TERM
exit "$FAILED"
