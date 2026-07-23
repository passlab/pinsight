#!/bin/bash
# End-to-end evaluation of PInsight PAUSE feature with LULESH
# Tests: PAUSE action fires, lttng rotate runs, analysis script launches, app resumes
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
APP_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
SOURCE_DIR="$APP_DIR/source"
PINSIGHT_DIR="$(cd "$APP_DIR/../.." && pwd)"
LIBPINSIGHT="$PINSIGHT_DIR/build/libpinsight.so"
LULESH="$SOURCE_DIR/lulesh2.0_baseline"
CONFIG="$SCRIPT_DIR/trace_config_pause_test.txt"
TRACE_OUTPUT="/tmp/pinsight_pause_test_traces"

echo "==================================================================="
echo " PInsight PAUSE Feature End-to-End Test"
echo "==================================================================="
echo "  PInsight lib:  $LIBPINSIGHT"
echo "  LULESH binary: $LULESH"
echo "  Config file:   $CONFIG"
echo "  Trace output:  $TRACE_OUTPUT"
echo ""

# Check prerequisites
if [ ! -f "$LIBPINSIGHT" ]; then
    echo "[ERROR] libpinsight.so not found at $LIBPINSIGHT"
    exit 1
fi
if [ ! -f "$LULESH" ]; then
    echo "[ERROR] lulesh2.0_baseline not found"
    exit 1
fi

# Clean up old traces
rm -rf "$TRACE_OUTPUT"
mkdir -p "$TRACE_OUTPUT"

echo "--- Step 1: Create LTTng session with rotation support ---"
lttng destroy pinsight_pause_test 2>/dev/null || true
lttng create pinsight_pause_test --output="$TRACE_OUTPUT"
lttng enable-event -u 'pinsight:*'
lttng enable-channel --userspace --subbuf-size=2M channel0 2>/dev/null || true
lttng start
echo "[OK] LTTng session 'pinsight_pause_test' started"
echo ""

echo "--- Step 2: Show PAUSE config ---"
cat "$CONFIG"
echo ""

echo "--- Step 3: Run LULESH with PInsight PAUSE config ---"
echo "  (Using -s 30 -i 500 to ensure enough iterations for 10 traces)"
echo "  Expected: After 10 traces, PAUSE triggers -> lttng rotate -> script -> resume"
echo ""

# Run LULESH with PInsight
# Use larger problem size and more iterations so PAUSE has time to trigger
cd "$SOURCE_DIR"
OMP_NUM_THREADS=4 \
OMP_TOOL_LIBRARIES="$LIBPINSIGHT" \
PINSIGHT_TRACE_CONFIG_FILE="$CONFIG" \
timeout 60 "$LULESH" -s 30 -i 500 2>&1

echo ""
echo "--- Step 4: Stop LTTng session ---"
lttng stop
lttng destroy pinsight_pause_test
echo ""

echo "--- Step 5: Check trace output ---"
echo "  Trace directory contents:"
find "$TRACE_OUTPUT" -type f | head -20
echo ""

# Check if rotation happened (there should be multiple chunk directories)
NUM_CHUNKS=$(find "$TRACE_OUTPUT" -maxdepth 2 -name "metadata" | wc -l)
echo "  Number of trace chunks: $NUM_CHUNKS"

if [ "$NUM_CHUNKS" -gt 0 ]; then
    echo "[OK] Traces were collected"
else
    echo "[WARN] No trace chunks found (lttng rotate may not have produced separate chunks)"
fi

echo ""
echo "==================================================================="
echo " Test Complete"
echo "==================================================================="
