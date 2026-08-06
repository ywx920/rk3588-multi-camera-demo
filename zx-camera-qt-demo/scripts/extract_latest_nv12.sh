#!/usr/bin/env bash
set -euo pipefail

DEMO_DIR="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
REPORT_DIR="${REPORT_DIR:-$DEMO_DIR/../zx-camera-minimal/board-reports}"
OUTPUT="${OUTPUT:-/tmp/zx-camera-sample-2112x1568-NV12.raw}"

LATEST="$(find "$REPORT_DIR" -maxdepth 1 -type f \
    -name 'zx-stage1-*.tar.gz' -printf '%T@ %p\n' |
    sort -nr | sed -n '1p' | cut -d' ' -f2-)"

if [ -z "$LATEST" ]; then
    echo "ERROR: no stage-one report found in $REPORT_DIR" >&2
    exit 1
fi

MEMBER="$(tar -tzf "$LATEST" | grep '/frames/.*\.raw$' |
    sort | sed -n '1p')"
if [ -z "$MEMBER" ]; then
    echo "ERROR: no raw frame found in $LATEST" >&2
    exit 1
fi

tar -xOzf "$LATEST" "$MEMBER" > "$OUTPUT"
echo "Report: $LATEST"
echo "Member: $MEMBER"
echo "Output: $OUTPUT"
stat -c 'Size: %s bytes' "$OUTPUT"
