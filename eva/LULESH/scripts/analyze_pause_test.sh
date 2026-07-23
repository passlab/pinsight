#!/bin/bash
# Analysis script that VERIFIES rotated traces using babeltrace2.
# Receives: <chunk_path> <app_pid> <config_file>
CHUNK_PATH=$1
APP_PID=$2
CONFIG_FILE=$3

echo "" >&2
echo "======================================================" >&2
echo "  PInsight Analysis Script — Trace Verification" >&2
echo "======================================================" >&2
echo "  Chunk path:  $CHUNK_PATH" >&2
echo "  App PID:     $APP_PID" >&2
echo "  Config file: $CONFIG_FILE" >&2
echo "  Timestamp:   $(date)" >&2

if [ -z "$CHUNK_PATH" ] || [ ! -d "$CHUNK_PATH" ]; then
    echo "  [FAIL] Chunk path missing or not a directory" >&2
    kill -USR1 $APP_PID 2>/dev/null
    exit 1
fi

echo "  [OK] Chunk directory exists" >&2

# Count events using babeltrace2
if command -v babeltrace2 &>/dev/null; then
    EVENT_COUNT=$(babeltrace2 "$CHUNK_PATH" 2>/dev/null | wc -l)
    echo "  [babeltrace2] Total events in rotated chunk: $EVENT_COUNT" >&2

    if [ "$EVENT_COUNT" -gt 0 ]; then
        echo "  [OK] Rotated chunk contains real trace data!" >&2
        echo "" >&2
        echo "  --- Event type breakdown ---" >&2
        babeltrace2 "$CHUNK_PATH" 2>/dev/null | grep -oP '\w+:\w+' | sort | uniq -c | sort -rn | head -10 | while read line; do
            echo "    $line" >&2
        done
        echo "" >&2
        echo "  --- First 3 PInsight events ---" >&2
        babeltrace2 "$CHUNK_PATH" 2>/dev/null | grep "ompt_pinsight" | head -3 | while read line; do
            echo "    $line" >&2
        done
    else
        echo "  [WARN] No events in rotated chunk" >&2
    fi
else
    echo "  [INFO] babeltrace2 not available, skipping event verification" >&2
    du -sh "$CHUNK_PATH" >&2
fi

echo "" >&2
echo "  Analysis complete. Sending SIGUSR1 to resume app..." >&2
sleep 1
kill -USR1 $APP_PID 2>/dev/null
echo "  [OK] SIGUSR1 sent to PID $APP_PID" >&2
echo "======================================================" >&2
