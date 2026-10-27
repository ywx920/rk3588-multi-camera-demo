#!/usr/bin/env bash
#
# Prepare the IQ compatibility test from the Ubuntu PC.
# It uploads source/config/scripts, compiles on RK3588, and installs the board
# test tools under /home/zxwy/zx-camera-tools. It does not modify /etc/iqfiles.

set -euo pipefail

PROJECT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
ASKPASS_SCRIPT="$PROJECT_DIR/scripts/pc_deploy_and_test.sh"
BOARD_IP="${BOARD_IP:-192.168.10.65}"
BOARD_USER="${BOARD_USER:-zxwy}"
BOARD_PASSWORD="${BOARD_PASSWORD:?Set BOARD_PASSWORD in the environment}"
BOARD_PORT="${BOARD_PORT:-22}"
REMOTE_TOOL_ROOT="/home/$BOARD_USER/zx-camera-tools"
REMOTE_SOURCE="$REMOTE_TOOL_ROOT/source/zx-camera-minimal"
REMOTE_BIN="$REMOTE_TOOL_ROOT/bin/zx-camera_minimal"
REMOTE_SCRIPT="$REMOTE_TOOL_ROOT/scripts/board_iq_compat_test.sh"
REMOTE_CHECK_SCRIPT="$REMOTE_TOOL_ROOT/scripts/board_camera_status_check.sh"
SDK_IQ="${SDK_IQ:-/home/wx/rk3588_sdk_restore/rk3588_sdk/ubuntu/overlay/etc/iqfiles/ov13850_ZC-OV13850R2A-V1_Largan-50064B31.json}"
REMOTE_SDK_IQ="$REMOTE_TOOL_ROOT/iq/reference/ov13850_ZC-sdk-isp30.json"
COMPAT_IQ="$PROJECT_DIR/iq-test/ov13850_ZC-isp30-compat-v3.json"
REMOTE_COMPAT_IQ="$REMOTE_TOOL_ROOT/iq/ov13850_ZC-isp30-compat-v3.json"
LOCAL_ARCHIVE="/tmp/zx-camera-iq-source-$$.tar.gz"
REMOTE_ARCHIVE="$REMOTE_TOOL_ROOT/source/zx-camera-iq-source.tar.gz"

cleanup()
{
    rm -f "$LOCAL_ARCHIVE"
}
trap cleanup EXIT

run_ssh()
{
    ZX_CAMERA_ASKPASS_MODE=1 \
    BOARD_PASSWORD="$BOARD_PASSWORD" \
    SSH_ASKPASS="$ASKPASS_SCRIPT" \
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
    SSH_ASKPASS="$ASKPASS_SCRIPT" \
    SSH_ASKPASS_REQUIRE=force \
    DISPLAY=:0 \
        setsid -w scp \
        -o PreferredAuthentications=password \
        -o PubkeyAuthentication=no \
        -o StrictHostKeyChecking=accept-new \
        "$@" < /dev/null
}

for tool in tar ssh scp setsid; do
    command -v "$tool" >/dev/null 2>&1 || {
        echo "ERROR: missing PC command: $tool" >&2
        exit 1
    }
done
if [ ! -f "$SDK_IQ" ]; then
    echo "ERROR: SDK ISP30 IQ not found: $SDK_IQ" >&2
    exit 1
fi
if [ ! -f "$COMPAT_IQ" ]; then
    echo "ERROR: generated compatibility IQ not found: $COMPAT_IQ" >&2
    exit 1
fi

echo "===== 1. Package camera source ====="
tar -czf "$LOCAL_ARCHIVE" \
    --exclude='zx-camera-minimal/build' \
    --exclude='zx-camera-minimal/build-board' \
    --exclude='zx-camera-minimal/board-reports' \
    -C "$(dirname "$PROJECT_DIR")" "$(basename "$PROJECT_DIR")"

echo "===== 2. Upload source and IQ test script ====="
run_ssh -p "$BOARD_PORT" "${BOARD_USER}@${BOARD_IP}" \
    "mkdir -p '$REMOTE_TOOL_ROOT'/bin '$REMOTE_TOOL_ROOT'/scripts \
       '$REMOTE_TOOL_ROOT'/iq/reference '$REMOTE_TOOL_ROOT'/reports \
       '$REMOTE_TOOL_ROOT'/source"
run_scp -P "$BOARD_PORT" "$LOCAL_ARCHIVE" \
    "${BOARD_USER}@${BOARD_IP}:${REMOTE_ARCHIVE}"
run_scp -P "$BOARD_PORT" \
    "$PROJECT_DIR/scripts/board_iq_compat_test.sh" \
    "${BOARD_USER}@${BOARD_IP}:${REMOTE_SCRIPT}"
run_scp -P "$BOARD_PORT" \
    "$PROJECT_DIR/scripts/board_camera_status_check.sh" \
    "${BOARD_USER}@${BOARD_IP}:${REMOTE_CHECK_SCRIPT}"
run_scp -P "$BOARD_PORT" "$SDK_IQ" \
    "${BOARD_USER}@${BOARD_IP}:${REMOTE_SDK_IQ}"
run_scp -P "$BOARD_PORT" "$COMPAT_IQ" \
    "${BOARD_USER}@${BOARD_IP}:${REMOTE_COMPAT_IQ}"

echo "===== 3. Extract and compile on RK3588 ====="
run_ssh -p "$BOARD_PORT" "${BOARD_USER}@${BOARD_IP}" \
    "rm -rf '$REMOTE_SOURCE' &&
     tar -xzf '$REMOTE_ARCHIVE' -C '$REMOTE_TOOL_ROOT/source' &&
     rm -f '$REMOTE_ARCHIVE' &&
     c++ -std=c++17 -O2 -Wall -Wextra -Wpedantic \
       -I'$REMOTE_SOURCE/include' \
       '$REMOTE_SOURCE/src/main.cpp' \
       '$REMOTE_SOURCE/src/camera_config.cpp' \
       '$REMOTE_SOURCE/src/v4l2_camera.cpp' \
       -o '$REMOTE_BIN' &&
     chmod +x '$REMOTE_SCRIPT' '$REMOTE_CHECK_SCRIPT' '$REMOTE_BIN' &&
     ls -l '$REMOTE_BIN' '$REMOTE_SCRIPT' '$REMOTE_CHECK_SCRIPT' '$REMOTE_SDK_IQ' \
       '$REMOTE_COMPAT_IQ' &&
     grep -o '\"'\"'\"scene_isp[0-9]*\"'\"'\"' '$REMOTE_COMPAT_IQ' | head -n 1 &&
     sha256sum '$REMOTE_COMPAT_IQ'"

echo
echo "Preparation complete. On the board run:"
echo "  sudo bash $REMOTE_SCRIPT --install-on-pass"
