#!/usr/bin/env bash
set -euo pipefail
project="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
askpass="$project/../zx-camera-minimal/scripts/pc_deploy_and_test.sh"
ip="${BOARD_IP:?Set BOARD_IP in the environment}"
user="${BOARD_USER:?Set BOARD_USER in the environment}"
password="${BOARD_PASSWORD:?Set BOARD_PASSWORD in the environment}"
port="${BOARD_PORT:-22}"
remote="${REMOTE_DIR:-/home/$user/zx-camera-qt-demo}"
archive="/tmp/zx-camera-qt-demo-$$.tar.gz"
trap 'rm -f "$archive"' EXIT
ssh_run()
{
    ZX_CAMERA_ASKPASS_MODE=1 BOARD_PASSWORD="$password" \
    SSH_ASKPASS="$askpass" SSH_ASKPASS_REQUIRE=force DISPLAY=:0 \
        setsid -w ssh -o PreferredAuthentications=password \
        -o PubkeyAuthentication=no -o StrictHostKeyChecking=accept-new \
        -p "$port" "$user@$ip" "$@" < /dev/null
}
scp_run()
{
    ZX_CAMERA_ASKPASS_MODE=1 BOARD_PASSWORD="$password" \
    SSH_ASKPASS="$askpass" SSH_ASKPASS_REQUIRE=force DISPLAY=:0 \
        setsid -w scp -o PreferredAuthentications=password \
        -o PubkeyAuthentication=no -o StrictHostKeyChecking=accept-new \
        -P "$port" "$@" < /dev/null
}
tar -czf "$archive" --exclude='.git' --exclude='.env' --exclude='.env.*' --exclude='.ssh' --exclude='id_rsa*' --exclude='id_ed25519*' --exclude='*.pem' --exclude='*.p12' --exclude='*.pfx' --exclude='*.key' --exclude=build --exclude=build-board -C "$project" .
ssh_run "mkdir -p '$remote'"
scp_run "$archive" "$user@$ip:$remote/source.tar.gz"
ssh_run "tar -xzf '$remote/source.tar.gz' -C '$remote' &&
    rm -f '$remote/source.tar.gz' &&
    chmod +x '$remote'/scripts/*.sh &&
    bash '$remote/scripts/check_board_env.sh' || true"
echo "Source deployed to: $user@$ip:$remote"
