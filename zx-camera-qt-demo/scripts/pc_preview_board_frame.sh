#!/usr/bin/env bash
#
# Fetch the newest NV12 frame from the RK3588 board and preview it on the PC.

set -euo pipefail

DEMO_DIR="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
ASKPASS_SCRIPT="$DEMO_DIR/../zx-camera-minimal/scripts/pc_deploy_and_test.sh"
BOARD_IP="${BOARD_IP:-192.168.10.65}"
BOARD_USER="${BOARD_USER:-zxwy}"
BOARD_PASSWORD="${BOARD_PASSWORD:-qwer1234}"
BOARD_PORT="${BOARD_PORT:-22}"
WIDTH="${WIDTH:-2112}"
HEIGHT="${HEIGHT:-1568}"
LOCAL_FRAME="${LOCAL_FRAME:-/tmp/zx-camera-latest-${WIDTH}x${HEIGHT}-NV12.raw}"

run_ssh()
{
    ZX_CAMERA_ASKPASS_MODE=1 BOARD_PASSWORD="$BOARD_PASSWORD" \
    SSH_ASKPASS="$ASKPASS_SCRIPT" SSH_ASKPASS_REQUIRE=force DISPLAY=:0 \
        setsid -w ssh \
        -o PreferredAuthentications=password \
        -o PubkeyAuthentication=no \
        -o StrictHostKeyChecking=accept-new \
        -p "$BOARD_PORT" "$BOARD_USER@$BOARD_IP" "$@" < /dev/null
}

run_scp()
{
    ZX_CAMERA_ASKPASS_MODE=1 BOARD_PASSWORD="$BOARD_PASSWORD" \
    SSH_ASKPASS="$ASKPASS_SCRIPT" SSH_ASKPASS_REQUIRE=force DISPLAY=:0 \
        setsid -w scp \
        -o PreferredAuthentications=password \
        -o PubkeyAuthentication=no \
        -o StrictHostKeyChecking=accept-new \
        -P "$BOARD_PORT" "$@" < /dev/null
}

REMOTE_FRAME="$(run_ssh \
    "find /home/zxwy/zx-camera-tools/reports -type f -name '*.raw' \
     -printf '%T@ %p\\n' | sort -nr | head -n 1 | cut -d' ' -f2-")"

if [ -z "$REMOTE_FRAME" ]; then
    echo "ERROR: no RAW frame found on board" >&2
    exit 1
fi

echo "Board frame: $REMOTE_FRAME"
run_scp "$BOARD_USER@$BOARD_IP:$REMOTE_FRAME" "$LOCAL_FRAME"

EXPECTED_SIZE=$((WIDTH * HEIGHT * 3 / 2))
ACTUAL_SIZE="$(stat -c '%s' "$LOCAL_FRAME")"
echo "PC frame   : $LOCAL_FRAME"
echo "Frame size : $ACTUAL_SIZE bytes"
if [ "$ACTUAL_SIZE" -ne "$EXPECTED_SIZE" ]; then
    echo "ERROR: expected NV12 size $EXPECTED_SIZE bytes" >&2
    exit 1
fi

if [ ! -x "$DEMO_DIR/build/zx-camera_qt_demo" ]; then
    bash "$DEMO_DIR/scripts/build_pc.sh"
fi

exec "$DEMO_DIR/build/zx-camera_qt_demo" \
    --input "$LOCAL_FRAME" --width "$WIDTH" --height "$HEIGHT" --fps 30
