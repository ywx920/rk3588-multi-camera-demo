#!/usr/bin/env bash
#
# RK3588 V4L2 第一阶段一键环境检查（只读，不设置摄像头格式）
# 用法：
#   bash scripts/inspect_camera.sh
#   bash scripts/inspect_camera.sh /dev/video22
#   bash scripts/inspect_camera.sh /dev/video22 /tmp/my-camera-report
#

set -u
set -o pipefail

REQUESTED_NODE="${1:-}"
REPORT_DIR="${2:-/tmp/zx-camera-inspect-$(date +%Y%m%d-%H%M%S)}"
STREAM_COUNT="${STREAM_COUNT:-30}"
STREAM_TIMEOUT="${STREAM_TIMEOUT:-30}"

mkdir -p "$REPORT_DIR" || {
    echo "ERROR: cannot create report directory: $REPORT_DIR" >&2
    exit 1
}

SUMMARY="$REPORT_DIR/summary.txt"
SYSTEM_LOG="$REPORT_DIR/system.txt"
NODE_LOG="$REPORT_DIR/video-nodes.txt"
MEDIA_LOG="$REPORT_DIR/media-topology.txt"
DETAIL_LOG="$REPORT_DIR/camera-detail.txt"
STREAM_LOG="$REPORT_DIR/camera-stream.txt"
DMESG_LOG="$REPORT_DIR/dmesg-tail.txt"

exec > >(tee -a "$SUMMARY") 2>&1

section()
{
    printf '\n===== %s =====\n' "$1"
}

run_logged()
{
    local log_file="$1"
    shift
    {
        printf '\n$'
        printf ' %q' "$@"
        printf '\n'
        "$@"
        local rc=$?
        printf '[exit_code=%d]\n' "$rc"
        return "$rc"
    } 2>&1 | tee -a "$log_file"
    return "${PIPESTATUS[0]}"
}

tool_path()
{
    command -v "$1" 2>/dev/null || true
}

video_name()
{
    local node="$1"
    local base
    base="$(basename "$node")"
    if [ -r "/sys/class/video4linux/$base/name" ]; then
        tr -d '\n' < "/sys/class/video4linux/$base/name"
    else
        printf 'unknown'
    fi
}

choose_node()
{
    local node name

    if [ -n "$REQUESTED_NODE" ]; then
        printf '%s' "$REQUESTED_NODE"
        return
    fi

    if [ -e /dev/video22 ]; then
        printf '/dev/video22'
        return
    fi

    for node in /dev/video*; do
        [ -e "$node" ] || continue
        name="$(video_name "$node")"
        case "$name" in
            *mainpath*|*Mainpath*|*rkisp*|*RKISP*)
                printf '%s' "$node"
                return
                ;;
        esac
    done

    for node in /dev/video*; do
        [ -e "$node" ] || continue
        printf '%s' "$node"
        return
    done
}

section "RK3588 camera inspection"
echo "Start time      : $(date --iso-8601=seconds 2>/dev/null || date)"
echo "Report directory: $REPORT_DIR"
echo "This script does not install packages or set the camera format."

section "System and tools"
{
    echo "===== uname -a ====="
    uname -a 2>&1
    echo
    echo "===== /etc/os-release ====="
    if [ -r /etc/os-release ]; then
        cat /etc/os-release
    else
        echo "NOT READABLE: /etc/os-release"
    fi
    echo
    echo "===== architecture ====="
    uname -m 2>&1
    getconf LONG_BIT 2>&1 || true
    echo
    echo "===== tools ====="
    for tool in cmake c++ g++ media-ctl v4l2-ctl timeout tar; do
        path="$(tool_path "$tool")"
        printf '%-12s %s\n' "$tool" "${path:-NOT FOUND}"
    done
    echo
    echo "===== versions ====="
    cmake --version 2>&1 | head -n 2 || true
    c++ --version 2>&1 | head -n 2 || true
    media-ctl --version 2>&1 || true
    v4l2-ctl --version 2>&1 || true
} | tee "$SYSTEM_LOG"

section "Video and media nodes"
{
    echo "===== /dev/video* ====="
    ls -l /dev/video* 2>&1 || true
    echo
    echo "===== /dev/media* ====="
    ls -l /dev/media* 2>&1 || true
    echo
    echo "===== video node names ====="
    found=0
    for node in /dev/video*; do
        [ -e "$node" ] || continue
        found=1
        printf '%-18s %s\n' "$node" "$(video_name "$node")"
    done
    [ "$found" -eq 1 ] || echo "No /dev/video* nodes found"
} | tee "$NODE_LOG"

section "Media Controller topology"
: > "$MEDIA_LOG"
if [ -z "$(tool_path media-ctl)" ]; then
    echo "SKIP: media-ctl not found" | tee -a "$MEDIA_LOG"
else
    media_found=0
    for media in /dev/media*; do
        [ -e "$media" ] || continue
        media_found=1
        echo "Inspecting $media"
        run_logged "$MEDIA_LOG" media-ctl -p -d "$media" || true
    done
    [ "$media_found" -eq 1 ] ||
        echo "SKIP: no /dev/media* nodes found" | tee -a "$MEDIA_LOG"
fi

CAMERA_NODE="$(choose_node)"
section "Selected camera node"
if [ -z "$CAMERA_NODE" ]; then
    echo "ERROR: no camera node was specified or discovered."
    echo "Run again with: bash $0 /dev/videoXX"
    exit 2
fi
echo "Node: $CAMERA_NODE"
echo "Name: $(video_name "$CAMERA_NODE")"

if [ ! -e "$CAMERA_NODE" ]; then
    echo "ERROR: node does not exist: $CAMERA_NODE"
    exit 2
fi
if [ ! -r "$CAMERA_NODE" ]; then
    echo "WARNING: node is not readable by user $(id -un)"
fi

section "V4L2 capabilities and formats"
: > "$DETAIL_LOG"
if [ -z "$(tool_path v4l2-ctl)" ]; then
    echo "SKIP: v4l2-ctl not found" | tee -a "$DETAIL_LOG"
else
    run_logged "$DETAIL_LOG" v4l2-ctl -d "$CAMERA_NODE" --all || true
    run_logged "$DETAIL_LOG" v4l2-ctl -d "$CAMERA_NODE" --list-formats-ext || true
    run_logged "$DETAIL_LOG" v4l2-ctl -d "$CAMERA_NODE" --get-fmt-video || true
    run_logged "$DETAIL_LOG" v4l2-ctl -d "$CAMERA_NODE" --get-fmt-video-mplane || true
fi

section "Bounded 30-frame MMAP test"
: > "$STREAM_LOG"
STREAM_RC=0
if [ -z "$(tool_path v4l2-ctl)" ]; then
    echo "SKIP: v4l2-ctl not found" | tee -a "$STREAM_LOG"
    STREAM_RC=127
elif [ -z "$(tool_path timeout)" ]; then
    echo "SKIP: timeout command not found; refusing an unbounded stream test" |
        tee -a "$STREAM_LOG"
    STREAM_RC=127
else
    echo "Timeout: ${STREAM_TIMEOUT}s; frames: $STREAM_COUNT"
    run_logged "$STREAM_LOG" timeout --signal=INT --kill-after=5s \
        "${STREAM_TIMEOUT}s" \
        v4l2-ctl -d "$CAMERA_NODE" \
        --stream-mmap=4 \
        --stream-count="$STREAM_COUNT" \
        --stream-to=/dev/null \
        --verbose || STREAM_RC=$?
fi

section "Recent kernel messages"
{
    echo '$ dmesg | tail -n 200'
    dmesg 2>&1 | tail -n 200
    echo "[pipeline_exit_code=${PIPESTATUS[0]}]"
} | tee "$DMESG_LOG"

section "Result"
echo "Selected node   : $CAMERA_NODE"
echo "Stream exit code: $STREAM_RC"
case "$STREAM_RC" in
    0)   echo "Stream result   : PASS (command completed)" ;;
    124) echo "Stream result   : FAIL (timed out)" ;;
    127) echo "Stream result   : SKIPPED (required command missing)" ;;
    *)   echo "Stream result   : FAIL (inspect camera-stream.txt)" ;;
esac

ARCHIVE=""
if [ -n "$(tool_path tar)" ]; then
    ARCHIVE="${REPORT_DIR%/}.tar.gz"
    if tar -czf "$ARCHIVE" -C "$(dirname "$REPORT_DIR")" \
        "$(basename "$REPORT_DIR")"; then
        echo "Archive         : $ARCHIVE"
    else
        echo "WARNING: failed to create archive"
        ARCHIVE=""
    fi
fi

echo "End time        : $(date --iso-8601=seconds 2>/dev/null || date)"
echo
echo "Please return the archive, or all *.txt files under:"
echo "  $REPORT_DIR"

# Environment inspection can still be useful when streaming fails. Return the
# stream status so CI/shell users can detect failure.
exit "$STREAM_RC"
