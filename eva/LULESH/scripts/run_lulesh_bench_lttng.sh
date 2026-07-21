#!/bin/bash
# PInsight Overhead Evaluation — LULESH 2.0 (with LTTng per config)
# BASELINE and TRACING_nosess run without LTTng.
# All other configs run with a per-config LTTng session.
# Usage: bash run_lulesh_bench_lttng.sh [size]

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
APP_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
SOURCE_DIR="$APP_DIR/source"
ROOT_DIR="$(cd "$APP_DIR/../.." && pwd)"
P=$ROOT_DIR/build/libpinsight.so
S="${1:-30}"
R=5
RATE_MON_CFG="$SCRIPT_DIR/trace_config_rate_monitor.txt"
RATE_OFF_CFG="$SCRIPT_DIR/trace_config_rate_off.txt"
TRACE_BASE="/tmp/pinsight_bench_traces"

get_time() {
    grep 'Elapsed time' /tmp/lulesh_out.txt | sed 's/.*= *\([0-9.]*\).*/\1/'
}

get_fom() {
    grep 'FOM' /tmp/lulesh_out.txt | sed 's/.*= *\([0-9.]*\).*/\1/'
}

# Run without LTTng (BASELINE and TRACING_nosess)
run_nolttng() {
    local label=$1; shift
    printf "%-30s" "$label:"
    for i in $(seq 1 $R); do
        "$@" "$SOURCE_DIR/lulesh2.0" -s $S >/tmp/lulesh_out.txt 2>/dev/null
        t=$(get_time)
        f=$(get_fom)
        printf "%10s" "$t"
    done
    printf "  FOM=%s" "$f"
    echo ""
}

# Run with LTTng session (managed inline, no LD_PRELOAD)
run_lttng() {
    local label=$1
    local trace_dir="${TRACE_BASE}/${label}_${T}T"
    shift

    printf "%-30s" "$label:"
    for i in $(seq 1 $R); do
        rm -rf "$trace_dir"
        # Create LTTng session
        lttng create "${label}_bench" --output="$trace_dir" >/dev/null 2>&1
        lttng enable-event -u 'ompt_pinsight_lttng_ust:*' >/dev/null 2>&1
        lttng enable-event -u 'pinsight_enter_exit_lttng_ust:*' >/dev/null 2>&1
        lttng enable-event -u 'pmpi_pinsight_lttng_ust:*' >/dev/null 2>&1
        lttng start >/dev/null 2>&1

        # Run LULESH with PInsight via OMP_TOOL_LIBRARIES (no LD_PRELOAD)
        env "$@" OMP_TOOL_LIBRARIES=$P "$SOURCE_DIR/lulesh2.0" -s $S \
            >/tmp/lulesh_out.txt 2>/dev/null

        # Stop LTTng session
        lttng stop >/dev/null 2>&1
        lttng destroy "${label}_bench" >/dev/null 2>&1

        t=$(get_time)
        f=$(get_fom)
        printf "%10s" "$t"
    done
    local sz=$(du -sh "$trace_dir" 2>/dev/null | cut -f1)
    printf "  FOM=%-12s Traces=%s" "$f" "$sz"
    echo ""
}

echo "================================================================"
echo "PInsight Overhead Evaluation — LULESH 2.0 (size=$S, with LTTng)"
echo "Date: $(date)"
echo "PInsight: $P"
echo "Runs per config: $R (all values are elapsed time in seconds)"
echo "================================================================"
echo ""

for T in 1 2 4 6; do
    echo "--- OMP_NUM_THREADS=$T ---"
    printf "%-30s" ""; for i in $(seq 1 $R); do printf "%10s" "run$i"; done; echo "  FOM          Traces"
    echo "------------------------------------------------------------------------------------"

    # BASELINE: no PInsight, no LTTng
    run_nolttng "BASELINE" env OMP_NUM_THREADS=$T OMP_TOOL=disabled

    # OFF: PInsight OFF mode with LTTng
    run_lttng "OFF" OMP_NUM_THREADS=$T PINSIGHT_TRACE_OPENMP=OFF

    # MONITORING: with LTTng
    run_lttng "MONITORING" OMP_NUM_THREADS=$T PINSIGHT_TRACE_OPENMP=MONITORING

    # TRACING (no session): no LTTng, just PInsight tracing to fast-path no-op
    run_nolttng "TRACING_nosess" env OMP_NUM_THREADS=$T PINSIGHT_TRACE_OPENMP=TRACING OMP_TOOL_LIBRARIES=$P

    # TRACING (with session): full tracing with LTTng
    run_lttng "TRACING_sess" OMP_NUM_THREADS=$T PINSIGHT_TRACE_OPENMP=TRACING

    # Rate-limited with mode_after=MONITORING
    run_lttng "RATE_100_1_MONITOR" OMP_NUM_THREADS=$T PINSIGHT_TRACE_OPENMP=TRACING PINSIGHT_TRACE_CONFIG_FILE=$RATE_MON_CFG

    # Rate-limited with mode_after=OFF
    run_lttng "RATE_100_1_OFF" OMP_NUM_THREADS=$T PINSIGHT_TRACE_OPENMP=TRACING PINSIGHT_TRACE_CONFIG_FILE=$RATE_OFF_CFG

    echo ""
done

echo "================================================================"
echo "Done."
