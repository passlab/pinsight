#!/bin/bash
# Full overhead evaluation: 6 configs × 6 thread counts × 5 runs
# Usage: bash run_bench.sh [N] [M]
#   Default: 256 256

P=/home/yyan7/work/tools/pinsight/build/libpinsight.so
N="${1:-256} ${2:-256}"
R=5
RATE_CFG="$(cd "$(dirname "$0")" && pwd)/trace_config_rate100.txt"

get_time() {
    grep 'OpenMP.*threads.*elasped time' /tmp/jb_out.txt | grep -oP '\d+$'
}

run() {
    local label=$1; shift
    printf "%-25s" "$label:"
    for i in $(seq 1 $R); do
        "$@" ./jacobi $N >/tmp/jb_out.txt 2>/dev/null
        printf "%6s" "$(get_time)"
    done
    echo ""
}

cd "$(dirname "$0")"

echo "================================================================"
echo "PInsight Overhead Evaluation — Jacobi Solver ($N)"
echo "Date: $(date)"
echo "================================================================"
echo ""

for T in 1 2 4 6 8 12; do
    echo "--- OMP_NUM_THREADS=$T ---"
    run "BASELINE"         env OMP_NUM_THREADS=$T OMP_TOOL=disabled
    run "OFF"              env OMP_NUM_THREADS=$T PINSIGHT_TRACE_OPENMP=OFF OMP_TOOL_LIBRARIES=$P
    run "MONITORING"       env OMP_NUM_THREADS=$T PINSIGHT_TRACE_OPENMP=MONITORING OMP_TOOL_LIBRARIES=$P
    run "TRACING_nosess"   env OMP_NUM_THREADS=$T PINSIGHT_TRACE_OPENMP=TRACING OMP_TOOL_LIBRARIES=$P

    # Setup LTTng session
    lttng destroy pinsight_bench 2>/dev/null
    lttng create pinsight_bench --output=/tmp/pinsight_traces_${T} 2>/dev/null
    lttng enable-event -u 'ompt_pinsight_lttng_ust:*' 2>/dev/null
    lttng start 2>/dev/null

    run "TRACING_sess_full" env OMP_NUM_THREADS=$T PINSIGHT_TRACE_OPENMP=TRACING OMP_TOOL_LIBRARIES=$P
    run "TRACING_sess_r100" env OMP_NUM_THREADS=$T PINSIGHT_TRACE_OPENMP=TRACING PINSIGHT_TRACE_CONFIG_FILE=$RATE_CFG OMP_TOOL_LIBRARIES=$P

    lttng stop 2>/dev/null
    lttng destroy pinsight_bench 2>/dev/null
    echo ""
done

echo "================================================================"
echo "Done."
