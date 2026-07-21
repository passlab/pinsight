#!/bin/bash
#
# Comprehensive bidirectional mode switch test for PInsight
# Exercises all 6 transitions in a single LULESH execution:
#
#   Phase 0: TRACING (initial, max_num_traces=50, ~4s)
#   Phase 1: MONITORING  (SIGUSR1, ~4s)     [TRACING -> MONITORING]
#   Phase 2: OFF         (SIGUSR1, ~4s)     [MONITORING -> OFF]
#   Phase 3: MONITORING  (SIGUSR1, ~4s)     [OFF -> MONITORING]
#   Phase 4: TRACING     (SIGUSR1, max=200, ~4s) [MONITORING -> TRACING]
#   Phase 5: OFF         (SIGUSR1, ~4s)     [TRACING -> OFF]
#   Phase 6: TRACING     (SIGUSR1, max=500, ~rest) [OFF -> TRACING]
#
# This covers all 6 transitions:
#   TRACING->MONITORING, MONITORING->OFF, OFF->MONITORING,
#   MONITORING->TRACING, TRACING->OFF, OFF->TRACING

set -e
trap '' USR1  # Parent shell ignores SIGUSR1

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
APP_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
SOURCE_DIR="$APP_DIR/source"
PINSIGHT_DIR="$(cd "$APP_DIR/../.." && pwd)"
P=$PINSIGHT_DIR/build/libpinsight.so
D=$SOURCE_DIR
CFGFILE=/tmp/pinsight_bidir_test_config.txt
OUTFILE=/tmp/pinsight_bidir_out.txt
ERRFILE=/tmp/pinsight_bidir_err.txt
INTERVAL=4  # seconds between mode switches

echo "=========================================="
echo " PInsight Bidirectional Mode Switch Test"
echo "=========================================="
echo ""

write_config() {
    local mode=$1
    local max_traces=$2
    local rate=$3
    cat > $CFGFILE << EOF
[OpenMP.global]
    trace_mode = $mode
[Lexgion.default]
    trace_starts_at = 0
    max_num_traces = $max_traces
    tracing_rate = $rate
EOF
}

# Phase 0 config: TRACING with max_num_traces=50
write_config TRACING 50 1

echo "Phase 0: Starting LULESH in TRACING mode (max_traces=50)"
OMP_NUM_THREADS=4 \
  PINSIGHT_TRACE_OPENMP=TRACING \
  PINSIGHT_TRACE_CONFIG_FILE=$CFGFILE \
  OMP_TOOL_LIBRARIES=$P \
  $D/lulesh2.0 -s 40 > $OUTFILE 2>$ERRFILE &
PID=$!
echo "  PID: $PID"

sleep 1
if ! kill -0 $PID 2>/dev/null; then
    echo "ERROR: LULESH died before first switch"
    cat $ERRFILE
    exit 1
fi

# --- Phase 1: TRACING -> MONITORING ---
sleep $INTERVAL
echo ""
echo "Phase 1: TRACING -> MONITORING (SIGUSR1 at +${INTERVAL}s)"
write_config MONITORING 50 1
kill -USR1 $PID

# --- Phase 2: MONITORING -> OFF ---
sleep $INTERVAL
echo "Phase 2: MONITORING -> OFF (SIGUSR1 at +$((INTERVAL*2))s)"
write_config OFF 50 1
kill -USR1 $PID

# --- Phase 3: OFF -> MONITORING ---
sleep $INTERVAL
echo "Phase 3: OFF -> MONITORING (SIGUSR1 at +$((INTERVAL*3))s)"
write_config MONITORING 50 1
kill -USR1 $PID

# --- Phase 4: MONITORING -> TRACING (with increased max_traces=200) ---
sleep $INTERVAL
echo "Phase 4: MONITORING -> TRACING (max_traces=200, SIGUSR1 at +$((INTERVAL*4))s)"
write_config TRACING 200 1
kill -USR1 $PID

# --- Phase 5: TRACING -> OFF ---
sleep $INTERVAL
echo "Phase 5: TRACING -> OFF (SIGUSR1 at +$((INTERVAL*5))s)"
write_config OFF 200 1
kill -USR1 $PID

# --- Phase 6: OFF -> TRACING (with increased max_traces=500) ---
sleep $INTERVAL
echo "Phase 6: OFF -> TRACING (max_traces=500, SIGUSR1 at +$((INTERVAL*6))s)"
write_config TRACING 500 1
kill -USR1 $PID

echo ""
echo "Waiting for LULESH to complete..."
wait $PID
RC=$?
echo "LULESH exit code: $RC"

echo ""
echo "=========================================="
echo " Results Summary"
echo "=========================================="
echo ""

# Extract key metrics
echo "--- Performance ---"
grep -E 'Elapsed time|FOM|Iteration count' $OUTFILE

echo ""
echo "--- Initial Mode ---"
grep -E '# mode:' $OUTFILE | head -3

echo ""
echo "--- Auto-trigger Messages ---"
grep 'Auto-trigger' $OUTFILE || echo "(none)"

echo ""
echo "--- Trace Report (first 10 lexgions) ---"
grep -E '^[0-9]+\s' $OUTFILE | head -10

echo ""
echo "--- Trace Count Analysis ---"
echo "Lexgions with non-zero trace counts:"
grep -E '^[0-9]+\s' $OUTFILE | awk '$5 != "0(0-0)" {print}' | head -20

echo ""
echo "--- Total trace events ---"
total_traces=$(grep -E '^[0-9]+\s' $OUTFILE | awk '{
    split($5, a, "(");
    sum += a[1];
} END {print sum}')
echo "Total traces across all lexgions: $total_traces"

echo ""
echo "--- Stderr (mode-related) ---"
grep -iE 'mode|reload|signal|error|warning|segfault' $ERRFILE 2>/dev/null | head -10

echo ""
if [ $RC -eq 0 ]; then
    echo "✅ TEST PASSED: LULESH completed successfully with all 6 mode transitions"
else
    echo "❌ TEST FAILED: Exit code $RC"
fi

# Save full reports
echo ""
echo "Full stdout: $OUTFILE"
echo "Full stderr: $ERRFILE"
