#!/bin/bash
# cyclic_analyze.sh — Analysis script for cyclic INTROSPECT test
# Called by PInsight control thread via posix_spawn
# Args: $1=chunk_path $2=app_pid $3=config_file

CHUNK_PATH="$1"
APP_PID="$2"
CONFIG_FILE="$3"

# Resolve paths relative to this script's location ($0 is the absolute path
# passed by the PInsight control thread, or the script path when run standalone)
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
APP_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
RESULTS_ROOT="$APP_DIR/results/$(hostname -s)"
mkdir -p "$RESULTS_ROOT"
LOG="$RESULTS_ROOT/cyclic_introspect_log.txt"

# Increment cycle counter (use a file-based counter)
COUNTER_FILE="$RESULTS_ROOT/cyclic_counter.txt"
if [ -f "$COUNTER_FILE" ]; then
    CYCLE=$(cat "$COUNTER_FILE")
else
    CYCLE=0
fi
CYCLE=$((CYCLE + 1))
echo "$CYCLE" > "$COUNTER_FILE"

TIMESTAMP=$(date '+%Y-%m-%d %H:%M:%S.%N')

echo "=== INTROSPECT Cycle $CYCLE ===" >> "$LOG"
echo "  Timestamp: $TIMESTAMP" >> "$LOG"
echo "  Chunk path: $CHUNK_PATH" >> "$LOG"
echo "  App PID: $APP_PID" >> "$LOG"
echo "  Config file: $CONFIG_FILE" >> "$LOG"

# Check if LTTng session is active and rotate traces
if command -v lttng &>/dev/null; then
    ROTATE_OUTPUT=$(lttng rotate 2>&1)
    echo "  LTTng rotate: $ROTATE_OUTPUT" >> "$LOG"
fi

# Log the current trace archive chunks
if [ -d "$CHUNK_PATH" ]; then
    CHUNKS=$(ls -dt "$CHUNK_PATH"/*/ 2>/dev/null | head -5)
    echo "  Recent chunks:" >> "$LOG"
    for c in $CHUNKS; do
        SIZE=$(du -sh "$c" 2>/dev/null | cut -f1)
        echo "    $c ($SIZE)" >> "$LOG"
    done
fi

echo "  Script completed in cycle $CYCLE" >> "$LOG"
echo "" >> "$LOG"

# Send SIGUSR1 to resume the application
kill -USR1 "$APP_PID"
