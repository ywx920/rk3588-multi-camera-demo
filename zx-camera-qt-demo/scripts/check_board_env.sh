#!/usr/bin/env bash
set -u
failed=0
for tool in c++ cmake qmake pkg-config; do
    if command -v "$tool" >/dev/null 2>&1; then
        echo "PASS command: $tool -> $(command -v "$tool")"
    else
        echo "MISS command: $tool"
        failed=1
    fi
done
if pkg-config --exists Qt5Core Qt5Gui Qt5Widgets 2>/dev/null; then
    echo "PASS Qt5 development: $(pkg-config --modversion Qt5Core)"
else
    echo "MISS Qt5 development metadata"
    failed=1
fi
ldconfig -p 2>/dev/null | grep -E 'libQt5(Core|Gui|Widgets)\.so\.5' || true
find /usr/lib/aarch64-linux-gnu/qt5/plugins -type f \
    \( -name 'libqjpeg.so' -o -name 'libqxcb.so' \) -print 2>/dev/null || true
for path in /dev/video22 \
    /dev/v4l/by-id/usb-Zhejiang_Dahua_Technology_Co._Ltd_Webcam_UC325_C0335D3A1E1B3F268-video-index0
do
    [ -e "$path" ] && echo "PASS node: $path -> $(readlink -f "$path")" ||
        echo "MISS node: $path"
done
echo "DISPLAY=${DISPLAY:-<empty>}"
echo "XDG_SESSION_TYPE=${XDG_SESSION_TYPE:-<empty>}"
[ "$failed" -eq 0 ] && echo "result=PASS" ||
    echo "result=MISSING_BUILD_DEPENDENCIES"
exit "$failed"
