#!/usr/bin/env bash
#
# PC 端一键上传并触发板端测试。
# 默认：zxwy@192.168.10.65
#
# 短测试：bash scripts/pc_deploy_and_test.sh
# 完整测试：bash scripts/pc_deploy_and_test.sh --long-test
#

set -euo pipefail

if [ "${ZX_CAMERA_ASKPASS_MODE:-0}" = "1" ]; then
    printf '%s\n' "${BOARD_PASSWORD:?Set BOARD_PASSWORD in the environment}"
    exit 0
fi

PROJECT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
SCRIPT_PATH="$PROJECT_DIR/scripts/pc_deploy_and_test.sh"
BOARD_IP="${BOARD_IP:-192.168.10.65}"
BOARD_USER="${BOARD_USER:-zxwy}"
BOARD_PASSWORD="${BOARD_PASSWORD:?Set BOARD_PASSWORD in the environment}"
BOARD_PORT="${BOARD_PORT:-22}"
REMOTE_PARENT="${REMOTE_PARENT:-/tmp/zx-deploy}"
REMOTE_PROJECT="$REMOTE_PARENT/$(basename "$PROJECT_DIR")"
LOCAL_REPORT_DIR="${LOCAL_REPORT_DIR:-$PROJECT_DIR/board-reports}"
LONG_ARG="${1:-}"

case "$LONG_ARG" in
    ""|--long-test) ;;
    *)
        echo "Usage: $0 [--long-test]" >&2
        exit 2
        ;;
esac

for tool in ping ssh scp tar setsid; do
    command -v "$tool" >/dev/null 2>&1 || {
        echo "ERROR: PC command not found: $tool" >&2
        exit 1
    }
done

TARGET="${BOARD_USER}@${BOARD_IP}"
LOCAL_ARCHIVE="/tmp/zx-camera-minimal-upload-$$.tar.gz"
REMOTE_ARCHIVE="/tmp/zx-camera-minimal-upload-$$.tar.gz"

cleanup()
{
    rm -f "$LOCAL_ARCHIVE"
}
trap cleanup EXIT

run_ssh()
{
    ZX_CAMERA_ASKPASS_MODE=1 \
    BOARD_PASSWORD="$BOARD_PASSWORD" \
    SSH_ASKPASS="$SCRIPT_PATH" \
    SSH_ASKPASS_REQUIRE=force \
    DISPLAY=:0 \
        setsid -w ssh \
        -o PreferredAuthentications=password \
        -o PubkeyAuthentication=no \
        -o StrictHostKeyChecking=accept-new \
        "$@" < /dev/null
}

run_scp()
{
    ZX_CAMERA_ASKPASS_MODE=1 \
    BOARD_PASSWORD="$BOARD_PASSWORD" \
    SSH_ASKPASS="$SCRIPT_PATH" \
    SSH_ASKPASS_REQUIRE=force \
    DISPLAY=:0 \
        setsid -w scp \
        -o PreferredAuthentications=password \
        -o PubkeyAuthentication=no \
        -o StrictHostKeyChecking=accept-new \
        "$@" < /dev/null
}

echo "===== PC step 1: test network ====="
ping -c 2 -W 2 "$BOARD_IP"

echo "===== PC step 2: test SSH ====="
run_ssh -p "$BOARD_PORT" -o ConnectTimeout=5 "$TARGET" \
    'echo "SSH connected: host=$(hostname), user=$(id -un)"'

echo "===== PC step 3: package project ====="
tar -czf "$LOCAL_ARCHIVE" --exclude='.git' --exclude='.env' --exclude='.env.*' --exclude='.ssh' --exclude='id_rsa*' --exclude='id_ed25519*' --exclude='*.pem' --exclude='*.p12' --exclude='*.pfx' --exclude='*.key' \
    -C "$(dirname "$PROJECT_DIR")" "$(basename "$PROJECT_DIR")"
ls -lh "$LOCAL_ARCHIVE"

echo "===== PC step 4: upload project ====="
run_scp -P "$BOARD_PORT" "$LOCAL_ARCHIVE" "${TARGET}:${REMOTE_ARCHIVE}"

echo "===== PC step 5: extract project on board ====="
run_ssh -p "$BOARD_PORT" "$TARGET" \
    "mkdir -p '$REMOTE_PARENT' &&
     tar -xzf '$REMOTE_ARCHIVE' -C '$REMOTE_PARENT' &&
     rm -f '$REMOTE_ARCHIVE'"

echo "===== PC step 6: compile and test on board ====="
BOARD_TEST_RC=0
run_ssh -p "$BOARD_PORT" "$TARGET" \
    "cd '$REMOTE_PROJECT' &&
     bash scripts/board_stage1_test.sh $LONG_ARG" ||
    BOARD_TEST_RC=$?

echo "===== PC step 7: download newest report ====="
REMOTE_REPORT="$(
    run_ssh -p "$BOARD_PORT" "$TARGET" \
        "ls -1t /tmp/zx-stage1-*.tar.gz 2>/dev/null | head -n 1"
)"

if [ -z "$REMOTE_REPORT" ]; then
    echo "ERROR: board report archive was not found" >&2
    exit 1
fi

mkdir -p "$LOCAL_REPORT_DIR"
run_scp -P "$BOARD_PORT" \
    "${TARGET}:${REMOTE_REPORT}" "$LOCAL_REPORT_DIR/"

echo
echo "All requested operations finished."
echo "Board project: $REMOTE_PROJECT"
echo "Local report : $LOCAL_REPORT_DIR/$(basename "$REMOTE_REPORT")"
echo "Board test RC: $BOARD_TEST_RC"

exit "$BOARD_TEST_RC"
