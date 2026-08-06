#!/usr/bin/env bash
#
# RK3588 板端阶段一自动测试。
# 短测试：bash scripts/board_stage1_test.sh
# 完整测试：bash scripts/board_stage1_test.sh --long-test
#

set -u
set -o pipefail

PROJECT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
DEVICE="${DEVICE:-/dev/video22}"
WIDTH="${WIDTH:-2112}"
HEIGHT="${HEIGHT:-1568}"
FORMAT="${FORMAT:-NV12}"
LONG_TEST=0

case "${1:-}" in
    "") ;;
    --long-test) LONG_TEST=1 ;;
    *)
        echo "Usage: $0 [--long-test]" >&2
        exit 2
        ;;
esac

STAMP="$(date +%Y%m%d-%H%M%S)"
REPORT_DIR="${REPORT_DIR:-/tmp/zx-stage1-$STAMP}"
FRAME_DIR="$REPORT_DIR/frames"
BUILD_DIR="$PROJECT_DIR/build-board"
MAIN_LOG="$REPORT_DIR/stage1-test.log"
FAILED=0

mkdir -p "$REPORT_DIR" "$FRAME_DIR" "$BUILD_DIR" || exit 1
exec > >(tee -a "$MAIN_LOG") 2>&1

section()
{
    printf '\n===== %s =====\n' "$1"
}

fail()
{
    echo "FAIL: $*"
    FAILED=1
}

section "Test configuration"
echo "project=$PROJECT_DIR"
echo "device=$DEVICE"
echo "requested=${WIDTH}x${HEIGHT} $FORMAT"
echo "report=$REPORT_DIR"
echo "long_test=$LONG_TEST"

section "Environment"
uname -a || fail "uname failed"
cat /etc/os-release 2>&1 || true
id
ls -l "$DEVICE" 2>&1 || fail "$DEVICE does not exist"

for tool in c++ timeout stat find sort tar tee; do
    if command -v "$tool" >/dev/null 2>&1; then
        echo "$tool=$(command -v "$tool")"
    else
        fail "required command not found: $tool"
    fi
done

if command -v v4l2-ctl >/dev/null 2>&1; then
    v4l2-ctl -d "$DEVICE" --all > "$REPORT_DIR/v4l2-all.txt" 2>&1 ||
        fail "v4l2-ctl --all failed"
    v4l2-ctl -d "$DEVICE" --list-formats-ext \
        > "$REPORT_DIR/v4l2-formats.txt" 2>&1 ||
        fail "v4l2-ctl --list-formats-ext failed"
else
    echo "WARNING: v4l2-ctl not found; C++ program will still be tested"
fi

if [ "$FAILED" -ne 0 ]; then
    echo "Environment check failed; stop before compilation."
    exit 1
fi

section "Compile on board"
c++ -std=c++17 -O2 -Wall -Wextra -Wpedantic \
    -I"$PROJECT_DIR/include" \
    "$PROJECT_DIR/src/main.cpp" \
    "$PROJECT_DIR/src/camera_config.cpp" \
    "$PROJECT_DIR/src/v4l2_camera.cpp" \
    -o "$BUILD_DIR/zx-camera_minimal"
COMPILE_RC=$?
if [ "$COMPILE_RC" -ne 0 ]; then
    fail "compilation failed, exit code=$COMPILE_RC"
    exit 1
fi
"$BUILD_DIR/zx-camera_minimal" --help

dmesg 2>&1 | tail -n 300 > "$REPORT_DIR/dmesg-before.txt" || true

section "Test A: capture 300 frames and save 3"
CAPTURE_RC=0
timeout --signal=INT --kill-after=5s 30s \
    "$BUILD_DIR/zx-camera_minimal" \
    --device "$DEVICE" \
    --width "$WIDTH" \
    --height "$HEIGHT" \
    --format "$FORMAT" \
    --frames 300 \
    --save-count 3 \
    --output "$FRAME_DIR" \
    --buffer-count 4 \
    --poll-timeout-ms 2000 \
    --log-interval 100 \
    2>&1 | tee "$REPORT_DIR/capture-300.txt" ||
    CAPTURE_RC="${PIPESTATUS[0]}"

if [ "$CAPTURE_RC" -ne 0 ]; then
    fail "300-frame capture failed, exit code=$CAPTURE_RC"
fi
grep -q "Total frames       : 300" "$REPORT_DIR/capture-300.txt" ||
    fail "summary does not report 300 frames"
grep -q "Sequence gaps      : 0" "$REPORT_DIR/capture-300.txt" ||
    fail "sequence gaps are non-zero or summary is missing"
grep -q "Duplicate sequences: 0" "$REPORT_DIR/capture-300.txt" ||
    fail "duplicate sequences are non-zero or summary is missing"
grep -q "Out-of-order seq   : 0" "$REPORT_DIR/capture-300.txt" ||
    fail "out-of-order sequences are non-zero or summary is missing"

section "Test B: validate saved frame files"
EXPECTED_SIZE=$((WIDTH * HEIGHT * 3 / 2))
FILE_COUNT=0
BAD_SIZE=0
echo "Expected NV12 size for requested geometry: $EXPECTED_SIZE bytes"

while IFS= read -r file; do
    [ -n "$file" ] || continue
    FILE_COUNT=$((FILE_COUNT + 1))
    size="$(stat -c %s "$file")"
    echo "$(basename "$file"): $size bytes"
    if [ "$FORMAT" = "NV12" ] && [ "$size" -ne "$EXPECTED_SIZE" ]; then
        BAD_SIZE=$((BAD_SIZE + 1))
        fail "unexpected size: $file ($size)"
    fi
done < <(find "$FRAME_DIR" -maxdepth 1 -type f -name '*.raw' | sort)

if [ "$FILE_COUNT" -ne 3 ]; then
    fail "expected 3 raw files, found $FILE_COUNT"
fi

section "Test C: SIGINT safe exit"
SIGINT_RC=0
timeout --signal=INT --kill-after=5s 5s \
    "$BUILD_DIR/zx-camera_minimal" \
    --device "$DEVICE" \
    --width "$WIDTH" \
    --height "$HEIGHT" \
    --format "$FORMAT" \
    --frames 0 \
    --save-count 0 \
    --output "$FRAME_DIR" \
    --poll-timeout-ms 2000 \
    --log-interval 100 \
    2>&1 | tee "$REPORT_DIR/sigint-test.txt" ||
    SIGINT_RC="${PIPESTATUS[0]}"

# GNU timeout 在到达时间限制并发送 SIGINT 后通常返回 124。
if [ "$SIGINT_RC" -ne 0 ] && [ "$SIGINT_RC" -ne 124 ]; then
    fail "SIGINT test returned unexpected code=$SIGINT_RC"
fi
grep -q "Capture summary" "$REPORT_DIR/sigint-test.txt" ||
    fail "program did not print a clean summary after SIGINT"

section "Test D: reopen device immediately"
REOPEN_RC=0
timeout --signal=INT --kill-after=5s 10s \
    "$BUILD_DIR/zx-camera_minimal" \
    --device "$DEVICE" \
    --width "$WIDTH" \
    --height "$HEIGHT" \
    --format "$FORMAT" \
    --frames 30 \
    --save-count 0 \
    --output "$FRAME_DIR" \
    --log-interval 30 \
    2>&1 | tee "$REPORT_DIR/reopen-30.txt" ||
    REOPEN_RC="${PIPESTATUS[0]}"

if [ "$REOPEN_RC" -ne 0 ]; then
    fail "device reopen test failed, exit code=$REOPEN_RC"
fi
grep -q "Total frames       : 30" "$REPORT_DIR/reopen-30.txt" ||
    fail "reopen test did not capture 30 frames"

LONG_RC="not_run"
if [ "$LONG_TEST" -eq 1 ]; then
    section "Test E: 10-minute stability"
    LONG_RC=0
    timeout --signal=INT --kill-after=5s 600s \
        "$BUILD_DIR/zx-camera_minimal" \
        --device "$DEVICE" \
        --width "$WIDTH" \
        --height "$HEIGHT" \
        --format "$FORMAT" \
        --frames 0 \
        --save-count 0 \
        --output "$FRAME_DIR" \
        --poll-timeout-ms 2000 \
        --log-interval 300 \
        2>&1 | tee "$REPORT_DIR/stability-10min.txt" ||
        LONG_RC="${PIPESTATUS[0]}"

    if [ "$LONG_RC" -ne 0 ] && [ "$LONG_RC" -ne 124 ]; then
        fail "10-minute test failed, exit code=$LONG_RC"
    fi
    grep -q "Capture summary" "$REPORT_DIR/stability-10min.txt" ||
        fail "10-minute test did not stop cleanly"
else
    section "Test E skipped"
    echo "Run with --long-test for the final 10-minute acceptance test."
fi

dmesg 2>&1 | tail -n 500 > "$REPORT_DIR/dmesg-after.txt" || true

section "Final result"
{
    echo "device=$DEVICE"
    echo "requested=${WIDTH}x${HEIGHT}_$FORMAT"
    echo "compile_exit_code=$COMPILE_RC"
    echo "capture_300_exit_code=$CAPTURE_RC"
    echo "saved_file_count=$FILE_COUNT"
    echo "bad_file_size_count=$BAD_SIZE"
    echo "sigint_exit_code=$SIGINT_RC"
    echo "reopen_exit_code=$REOPEN_RC"
    echo "long_test_exit_code=$LONG_RC"
    if [ "$FAILED" -eq 0 ]; then
        echo "result=PASS_FOR_EXECUTED_ITEMS"
    else
        echo "result=FAIL"
    fi
} | tee "$REPORT_DIR/result.txt"

ARCHIVE="${REPORT_DIR}.tar.gz"
if tar -czf "$ARCHIVE" -C "$(dirname "$REPORT_DIR")" \
    "$(basename "$REPORT_DIR")"; then
    echo "Report archive: $ARCHIVE"
else
    fail "cannot create report archive"
fi

exit "$FAILED"
