#!/bin/bash
# TBB Proxy Allocation Benchmarking for PInsight vs HPCToolkit
# 8 Threads

set -e
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
APP_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
SOURCE_DIR="$APP_DIR/source"
PINSIGHT_DIR="$(cd "$APP_DIR/../.." && pwd)"
PINSIGHT_LIB="$PINSIGHT_DIR/build/libpinsight.so"
TRACE_SH="$PINSIGHT_DIR/scripts/trace.sh"
TBB_MALLOC="${TBB_MALLOC:-/home/yyan7/tools/hpctoolkit-install/lib/x86_64-linux-gnu/libtbbmalloc_proxy.so}"
LULESH="$SOURCE_DIR/lulesh2.0"
S="30"
R="5"
TDIR="/tmp/e1_tbb_traces"

CFG_STANDBY="$SCRIPT_DIR/e1_trace_50_standby.txt"

HPCRUN="${HPCRUN:-hpcrun}"
HPCTOOLKIT_LIBDIR="${HPCTOOLKIT_LIBDIR:-/home/yyan7/tools/hpctoolkit-install/lib/x86_64-linux-gnu}"
export OMP_NUM_THREADS=8
export LD_LIBRARY_PATH="$HPCTOOLKIT_LIBDIR:$LD_LIBRARY_PATH"

echo "================================================================"
echo "=== Complete TBB Allocator Evaluation (OMP_NUM_THREADS=8) ==="
echo "================================================================"
printf "%-35s" ""; for i in $(seq 1 $R); do printf "%10s" "run$i"; done; echo ""
echo "------------------------------------------------------------------------"

get_time() {
    grep 'Elapsed time' /tmp/lulesh_tbb.log | grep -oP '[\d.]+(?= \(s\))'
}

run() {
    local label=$1; shift
    printf "%-35s" "$label:"
    for i in $(seq 1 $R); do
        "$@" -s $S >/tmp/lulesh_tbb.log 2>&1 || true
        t=$(get_time)
        if [ -z "$t" ]; then t="ERR"; fi
        printf "%10s" "$t"
    done
    echo ""
}

run_pinsight() {
    local label=$1; shift
    local trace_dest=$1; shift
    printf "%-35s" "$label:"
    for i in $(seq 1 $R); do
        "$@" >/tmp/lulesh_tbb.log 2>&1 || true
        t=$(get_time)
        if [ -z "$t" ]; then t="ERR"; fi
        printf "%10s" "$t"
    done
    echo ""
}

# Clean trace dir
rm -rf "$TDIR"
mkdir -p "$TDIR"

# 1. Baseline
run "Baseline" env OMP_TOOL=disabled $LULESH

# 2. Baseline + TBB
run "Baseline_TBB" env OMP_TOOL=disabled LD_PRELOAD=$TBB_MALLOC $LULESH

# 3. PInsight Full Trace
run_pinsight "PInsight_Full" "$TDIR/p_full" \
    bash "$TRACE_SH" "$TDIR/p_full" "lulesh_full" "$PINSIGHT_LIB" ":" \
    $LULESH -s $S

# 4. PInsight 50 -> STANDBY
run_pinsight "PInsight_50_STANDBY" "$TDIR/p_standby" \
    env PINSIGHT_TRACE_CONFIG_FILE=$CFG_STANDBY \
    bash "$TRACE_SH" "$TDIR/p_standby" "lulesh_standby" "$PINSIGHT_LIB" ":" \
    $LULESH -s $S

# 5. PInsight Full Trace + TBB
run_pinsight "PInsight_Full_TBB" "$TDIR/p_full_tbb" \
    env LD_PRELOAD=$TBB_MALLOC \
    bash "$TRACE_SH" "$TDIR/p_full_tbb" "lulesh_full_tbb" "$PINSIGHT_LIB" ":" \
    $LULESH -s $S

# 6. PInsight 50 -> STANDBY + TBB
run_pinsight "PInsight_50_STANDBY_TBB" "$TDIR/p_standby_tbb" \
    env PINSIGHT_TRACE_CONFIG_FILE=$CFG_STANDBY LD_PRELOAD=$TBB_MALLOC \
    bash "$TRACE_SH" "$TDIR/p_standby_tbb" "lulesh_standby_tbb" "$PINSIGHT_LIB" ":" \
    $LULESH -s $S

# 7. HPCToolkit
# (Note: hpcrun uses its own allocator proxy injection under the hood)
run "HPCToolkit" "$HPCRUN" -o "$TDIR/hpctoolkit" -e REALTIME@1000 -t $LULESH

echo "================================================================"
echo "Done."
