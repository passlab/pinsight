#!/bin/bash
# run_cyclic_introspect_test.sh — Evaluate cyclic INTROSPECT feature
# Tests: multi-cycle trace→analyze→resume loop, trace_counter reset,
#        posix_spawn reliability, trace rotation per cycle

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
APP_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
SOURCE_DIR="$APP_DIR/source"
PINSIGHT_DIR="$(cd "$APP_DIR/../.." && pwd)"
RESULTS_ROOT="$APP_DIR/results/$(hostname -s)"
LULESH_BIN="$SOURCE_DIR/lulesh2.0"
PINSIGHT_LIB="$PINSIGHT_DIR/build/libpinsight.so"
CONFIG="$SCRIPT_DIR/cyclic_introspect.cfg"
LOG="$RESULTS_ROOT/cyclic_introspect_log.txt"
COUNTER_FILE="$RESULTS_ROOT/cyclic_counter.txt"
RESULT_DIR="$RESULTS_ROOT/results_cyclic"

# Use 24 threads on NUMA nodes 0-3
THREADS=24
LULESH_SIZE=20  # Small size for faster iteration, 500 iters
LULESH_ITERS=500

echo "=== Cyclic INTROSPECT Evaluation ==="
echo "  LULESH: ${LULESH_SIZE}^3, $LULESH_ITERS iters, $THREADS threads"
echo "  Config: max_num_traces=30, resume=TRACING, timeout=10s"
echo "  Expected cycles: ~$((LULESH_ITERS / 30)) (assuming ~30 iters per cycle)"
echo ""

# Clean up from previous runs
rm -f "$LOG" "$COUNTER_FILE"
mkdir -p "$RESULT_DIR"

# Destroy any leftover LTTng session
lttng destroy cyclic_introspect 2>/dev/null || true

# Create LTTng session
echo "--- Setting up LTTng session ---"
lttng create cyclic_introspect
lttng enable-event -u 'ompt_pinsight_lttng_ust:*'
lttng start
echo ""

# Run LULESH with cyclic INTROSPECT
echo "--- Running LULESH with cyclic INTROSPECT ---"
START_TIME=$(date +%s%N)

numactl --cpunodebind=0-3 --membind=0-3 \
    env OMP_NUM_THREADS=$THREADS \
    OMP_PLACES=cores \
    OMP_PROC_BIND=close \
    LD_PRELOAD="$PINSIGHT_LIB" \
    LD_LIBRARY_PATH=/usr/lib/llvm-21/lib:$LD_LIBRARY_PATH \
    PINSIGHT_TRACE_CONFIG_FILE="$CONFIG" \
    "$LULESH_BIN" -s $LULESH_SIZE -i $LULESH_ITERS 2>"$RESULT_DIR/stderr.log"

END_TIME=$(date +%s%N)
ELAPSED_MS=$(( (END_TIME - START_TIME) / 1000000 ))

echo "  LULESH completed in ${ELAPSED_MS}ms"
echo ""

# Stop LTTng
lttng stop
lttng destroy cyclic_introspect
echo ""

# ===================== Analysis =====================
echo "=== Results ==="

# Check cycle count
if [ -f "$COUNTER_FILE" ]; then
    CYCLES=$(cat "$COUNTER_FILE")
    echo "  Total INTROSPECT cycles: $CYCLES"
else
    CYCLES=0
    echo "  ERROR: No INTROSPECT cycles fired!"
fi

# Check PInsight control thread log
echo ""
echo "--- PInsight Control Thread Messages ---"
grep -E "INTROSPECT|Control thread|Auto-trigger|Application paused|Application resumed|posix_spawn|Launched|mode ->" \
    "$RESULT_DIR/stderr.log" 2>/dev/null | head -40
echo ""

# Check analysis script log
echo "--- Analysis Script Log ---" 
if [ -f "$LOG" ]; then
    cat "$LOG"
else
    echo "  No analysis script log found!"
fi

# Check trace chunks  
echo ""
echo "--- LTTng Trace Summary ---"
TRACE_DIR="$HOME/lttng-traces"
LATEST_TRACE=$(ls -dt "$TRACE_DIR"/cyclic_introspect-*/ 2>/dev/null | head -1)
if [ -n "$LATEST_TRACE" ]; then
    echo "  Trace dir: $LATEST_TRACE"
    # Count total events
    TOTAL_EVENTS=$(babeltrace2 "$LATEST_TRACE" 2>/dev/null | wc -l)
    echo "  Total trace events: $TOTAL_EVENTS"
    
    # Check for rotation chunks (subdirectories)
    CHUNK_COUNT=$(find "$LATEST_TRACE" -maxdepth 1 -type d | wc -l)
    echo "  Archive chunks: $((CHUNK_COUNT - 1))"
    
    # Event breakdown by type
    echo "  Event breakdown:"
    babeltrace2 "$LATEST_TRACE" 2>/dev/null | \
        grep -oP 'ompt_pinsight_lttng_ust:\K[a-z_]+' | \
        sort | uniq -c | sort -rn | head -10
fi

# Save summary
echo ""
echo "=== Summary ==="
echo "  Cycles: $CYCLES"
echo "  Runtime: ${ELAPSED_MS}ms"
echo "  Events: ${TOTAL_EVENTS:-unknown}"

# Copy results
cp "$LOG" "$RESULT_DIR/" 2>/dev/null || true
cp "$RESULT_DIR/stderr.log" "$RESULT_DIR/" 2>/dev/null || true

echo ""
echo "Results saved to: $RESULT_DIR/"
