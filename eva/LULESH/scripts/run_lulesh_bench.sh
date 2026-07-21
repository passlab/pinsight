#!/bin/bash
# PInsight Overhead Evaluation — LULESH 2.0
# Usage: bash run_lulesh_bench.sh [size]
#   Default size: 30 (30^3 mesh)

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
APP_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
SOURCE_DIR="$APP_DIR/source"
PINSIGHT_DIR="$(cd "$APP_DIR/../.." && pwd)"
P=$PINSIGHT_DIR/build/libpinsight.so
S="${1:-30}"
R=5
RATE_MON_CFG="$SCRIPT_DIR/trace_config_rate_monitor.txt"
RATE_OFF_CFG="$SCRIPT_DIR/trace_config_rate_off.txt"

get_time() {
    grep 'Elapsed time' /tmp/lulesh_out.txt | grep -oP '[\d.]+(?= \(s\))'
}

get_fom() {
    grep 'FOM' /tmp/lulesh_out.txt | grep -oP '[\d.]+(?= \(z/s\))'
}

run() {
    local label=$1; shift
    printf "%-30s" "$label:"
    for i in $(seq 1 $R); do
        "$@" "$SOURCE_DIR/lulesh2.0" -s $S >/tmp/lulesh_out.txt 2>/dev/null
        t=$(get_time)
        f=$(get_fom)
        printf "%10s" "$t"
    done
    # Print FOM from last run
    printf "  FOM=%s" "$f"
    echo ""
}

echo "================================================================"
echo "PInsight Overhead Evaluation — LULESH 2.0 (size=$S)"
echo "Date: $(date)"
echo "PInsight: $P"
echo "Runs per config: $R (all values are elapsed time in seconds)"
echo "================================================================"
echo ""

for T in 1 2 4 6; do
    echo "--- OMP_NUM_THREADS=$T ---"
    printf "%-30s" ""; for i in $(seq 1 $R); do printf "%10s" "run$i"; done; echo "  FOM"
    echo "------------------------------------------------------------------------"

    run "BASELINE"             env OMP_NUM_THREADS=$T OMP_TOOL=disabled
    run "OFF"                  env OMP_NUM_THREADS=$T PINSIGHT_TRACE_OPENMP=OFF OMP_TOOL_LIBRARIES=$P
    run "MONITORING"           env OMP_NUM_THREADS=$T PINSIGHT_TRACE_OPENMP=MONITORING OMP_TOOL_LIBRARIES=$P
    run "TRACING_nosess"       env OMP_NUM_THREADS=$T PINSIGHT_TRACE_OPENMP=TRACING OMP_TOOL_LIBRARIES=$P

    # LTTng session for full tracing
    lttng destroy pinsight_bench 2>/dev/null
    lttng create pinsight_bench --output=/tmp/pinsight_lulesh_traces_${T} 2>/dev/null
    lttng enable-event -u 'ompt_pinsight_lttng_ust:*' 2>/dev/null
    lttng enable-event -u 'pinsight_enter_exit_lttng_ust:*' 2>/dev/null
    lttng start 2>/dev/null

    run "TRACING_sess"         env OMP_NUM_THREADS=$T PINSIGHT_TRACE_OPENMP=TRACING OMP_TOOL_LIBRARIES=$P

    # Rate-limited with mode_after=MONITORING
    run "RATE_100_1_MONITOR"   env OMP_NUM_THREADS=$T PINSIGHT_TRACE_OPENMP=TRACING PINSIGHT_TRACE_CONFIG_FILE=$RATE_MON_CFG OMP_TOOL_LIBRARIES=$P

    # Rate-limited with mode_after=OFF
    run "RATE_100_1_OFF"       env OMP_NUM_THREADS=$T PINSIGHT_TRACE_OPENMP=TRACING PINSIGHT_TRACE_CONFIG_FILE=$RATE_OFF_CFG OMP_TOOL_LIBRARIES=$P

    lttng stop 2>/dev/null
    lttng destroy pinsight_bench 2>/dev/null

    # Report trace size
    if [ -d /tmp/pinsight_lulesh_traces_${T} ]; then
        TSIZE=$(du -sh /tmp/pinsight_lulesh_traces_${T} 2>/dev/null | cut -f1)
        echo "Trace volume (${T}T): $TSIZE"
    fi
    echo ""
done

echo "================================================================"
echo "Done."
