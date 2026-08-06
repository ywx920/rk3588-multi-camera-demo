#!/usr/bin/env bash
set -euo pipefail

project="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
duration="${DURATION_SECONDS:-86400}"
interval="${INTERVAL_SECONDS:-10}"
report_dir="${REPORT_DIR:-$project/reports}"
mkdir -p "$report_dir"
timestamp="$(date +%Y%m%d-%H%M%S)"
csv="$report_dir/stability-$timestamp.csv"
events="$report_dir/stability-$timestamp-events.log"

echo 'time,uptime_s,process,pid,cpu_percent,mem_percent,rss_kb,threads,temp_mC,npu_load,camera_status' >"$csv"
start="$(date +%s)"
end=$((start + duration))
echo "Monitoring for ${duration}s; interval=${interval}s"
echo "CSV: $csv"

while (( $(date +%s) < end )); do
    now="$(date --iso-8601=seconds)"
    uptime_s="$(cut -d. -f1 /proc/uptime)"
    pid="$(pgrep -n -x zx-camera_web || pgrep -n -x zx-camera_qt_de || true)"
    if [[ -n "$pid" ]]; then
        read -r process cpu mem rss threads < <(
            ps -p "$pid" -o comm=,%cpu=,%mem=,rss=,nlwp=)
    else
        process=none cpu=0 mem=0 rss=0 threads=0
    fi
    temp="$(find /sys/class/thermal -name temp -type f -print0 2>/dev/null |
        xargs -0 -r cat 2>/dev/null | sort -nr | head -n1)"
    temp="${temp:-0}"
    npu_load="$(find /sys -path '*rknpu*' -name load -type f -print -quit \
        2>/dev/null || true)"
    if [[ -n "$npu_load" ]]; then
        npu_load="$(tr -d '\n,' <"$npu_load" 2>/dev/null || echo unknown)"
    else
        npu_load=unknown
    fi
    camera_status="$(curl -fsS --max-time 2 http://127.0.0.1:8080/status \
        2>/dev/null | tr -d '\n,' || true)"
    camera_status="${camera_status:-qt-or-unavailable}"
    printf '%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,"%s"\n' \
        "$now" "$uptime_s" "$process" "${pid:-0}" "$cpu" "$mem" \
        "$rss" "$threads" "$temp" "$npu_load" "$camera_status" >>"$csv"
    sleep "$interval"
done

{
    echo "===== completed $(date --iso-8601=seconds) ====="
    echo "===== camera/NPU/RGA kernel events ====="
    dmesg 2>/dev/null | grep -iE \
        'rknpu|rga|rkisp|rkcif|csi2|iommu|overflow|timeout|fault|reset' |
        tail -n 500 || true
} >"$events"
echo "Completed: $csv"
echo "Events: $events"
