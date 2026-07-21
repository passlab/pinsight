#!/bin/bash
# E1 LULESH Overhead Evaluation — OpenMP Scalability
# Thread counts: 1, 2, 4, 8, 16, 24, 32
# Metrics: execution time, trace file sizes
# Tools: Baseline, PInsight (4 configs), Nsight Systems, HPCToolkit, Score-P

set +e  # Don't abort on individual tool failures
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
APP_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
SOURCE_DIR="$APP_DIR/source"
PINSIGHT_DIR="$(cd "$APP_DIR/../.." && pwd)"
PINSIGHT_LIB="$PINSIGHT_DIR/build/libpinsight.so"
TRACE_SH="$PINSIGHT_DIR/scripts/trace.sh"
S="30"
R="5"
TRACE_DIR="/tmp/e1_traces"

# Trace config files for PInsight dynamic modes (50 traces)
CFG_OFF="$SCRIPT_DIR/e1_trace_50_off.txt"
CFG_STANDBY="$SCRIPT_DIR/e1_trace_50_standby.txt"
CFG_MONITOR="$SCRIPT_DIR/e1_trace_50_monitor.txt"

THREAD_COUNTS="1 2 4 8 16 24 32"

echo "================================================================"
echo "E1: LULESH 2.0 OpenMP Overhead Evaluation"
echo "  Size=$S, Runs=$R"
echo "  Thread counts: $THREAD_COUNTS"
echo "  Date: $(date)"
echo "================================================================"
echo ""

get_time() {
    grep 'Elapsed time' /tmp/lulesh_out.txt | grep -oP '[\d.]+(?= \(s\))'
}

get_trace_size() {
    if [ -e "$1" ]; then
        du -sh "$1" 2>/dev/null | cut -f1
    else
        echo "N/A"
    fi
}

# run LABEL CMD...
# Runs CMD R times, printing elapsed time for each run.
run() {
    local label=$1; shift
    printf "%-30s" "$label:"
    for i in $(seq 1 $R); do
        "$@" -s $S >/tmp/lulesh_out.txt 2>/tmp/lulesh_err.txt
        t=$(get_time)
        if [ -z "$t" ]; then t="ERR"; fi
        printf "%10s" "$t"
    done
    echo ""
}

# run_pinsight LABEL TRACE_OUTPUT_DIR [EXTRA_ENV...] PROG ARGS...
# Uses scripts/trace.sh to launch PInsight with proper LTTng session.
# Runs R times, printing elapsed time for each run.
run_pinsight() {
    local label=$1; shift
    local trace_dest=$1; shift
    printf "%-30s" "$label:"
    for i in $(seq 1 $R); do
        "$@" >/tmp/lulesh_out.txt 2>/dev/null
        t=$(get_time)
        printf "%10s" "$t"
    done
    echo ""
}

# ---- Build LULESH (standard) ----
cd "$SOURCE_DIR"
make clean >/dev/null 2>&1 || true
make -j20 >/dev/null 2>&1

LULESH="$SOURCE_DIR/lulesh2.0"

# ---- Build LULESH with Score-P instrumentation ----
export PATH=$HOME/tools/scorep/bin:$PATH
make clean >/dev/null 2>&1 || true
make CXX="scorep g++ -DUSE_MPI=0" LULESH_EXEC=lulesh2.0_scorep LDFLAGS="-g -O3 -fopenmp" -j20 >/dev/null 2>&1

LULESH_SCOREP="$SOURCE_DIR/lulesh2.0_scorep"

# Rebuild the normal binary (Score-P make clean removed .o files)
make clean >/dev/null 2>&1 || true
make -j20 >/dev/null 2>&1

# ---- Setup HPCToolkit ----
export PATH=$HOME/tools/hpctoolkit-install/bin:$PATH

for T in $THREAD_COUNTS; do
    echo "================================================================"
    echo "=== OMP_NUM_THREADS=$T ==="
    echo "================================================================"
    printf "%-30s" ""; for i in $(seq 1 $R); do printf "%10s" "run$i"; done; echo ""
    echo "------------------------------------------------------------------------"

    export OMP_NUM_THREADS=$T

    # Clean up trace output directory for this thread count
    TDIR="$TRACE_DIR/${T}t"
    rm -rf "$TDIR"
    mkdir -p "$TDIR"

    # --- Baseline (no tool) ---
    run "Baseline"               env OMP_TOOL=disabled $LULESH

    # --- PInsight: Full Tracing (via trace.sh) ---
    run_pinsight "PInsight_Full" "$TDIR/pinsight_full" \
        bash "$TRACE_SH" "$TDIR/pinsight_full" "lulesh_full" "$PINSIGHT_LIB" ":" \
        $LULESH -s $S

    # --- PInsight: 50 traces -> OFF (via trace.sh) ---
    run_pinsight "PInsight_50_OFF" "$TDIR/pinsight_50_off" \
        env PINSIGHT_TRACE_CONFIG_FILE=$CFG_OFF \
        bash "$TRACE_SH" "$TDIR/pinsight_50_off" "lulesh_50off" "$PINSIGHT_LIB" ":" \
        $LULESH -s $S

    # --- PInsight: 50 traces -> STANDBY (via trace.sh) ---
    run_pinsight "PInsight_50_STANDBY" "$TDIR/pinsight_50_standby" \
        env PINSIGHT_TRACE_CONFIG_FILE=$CFG_STANDBY \
        bash "$TRACE_SH" "$TDIR/pinsight_50_standby" "lulesh_50standby" "$PINSIGHT_LIB" ":" \
        $LULESH -s $S

    # --- PInsight: 50 traces -> MONITORING (via trace.sh) ---
    run_pinsight "PInsight_50_MONITOR" "$TDIR/pinsight_50_monitor" \
        env PINSIGHT_TRACE_CONFIG_FILE=$CFG_MONITOR \
        bash "$TRACE_SH" "$TDIR/pinsight_50_monitor" "lulesh_50monitor" "$PINSIGHT_LIB" ":" \
        $LULESH -s $S

    # --- Nsight Systems ---
    rm -f "$TDIR/lulesh_nsys"*.nsys-rep
    run "Nsight_Systems"         nsys profile -y 60 -f true --trace=openmp,osrt -o "$TDIR/lulesh_nsys" $LULESH

    # --- HPCToolkit ---
    rm -rf "$TDIR/hpctoolkit-measurements"
    run "HPCToolkit"             hpcrun -o "$TDIR/hpctoolkit-measurements" -e REALTIME@1000 -t $LULESH

    # --- Score-P ---
    export SCOREP_ENABLE_TRACING=true
    export SCOREP_ENABLE_PROFILING=false
    export SCOREP_TOTAL_MEMORY=4G
    # Score-P aborts if experiment dir already exists, so we use overwrite mode
    export SCOREP_EXPERIMENT_DIRECTORY="$TDIR/scorep_traces"
    export SCOREP_OVERWRITE_EXPERIMENT_DIRECTORY=true
    rm -rf "$TDIR/scorep_traces"
    run "Score-P"                $LULESH_SCOREP

    # ---- Trace Volume Summary ----
    echo ""
    echo "--- Trace Sizes (Thread=$T) ---"
    printf "  %-30s %s\n" "PInsight_Full:"      "$(get_trace_size "$TDIR/pinsight_full")"
    printf "  %-30s %s\n" "PInsight_50_OFF:"     "$(get_trace_size "$TDIR/pinsight_50_off")"
    printf "  %-30s %s\n" "PInsight_50_STANDBY:" "$(get_trace_size "$TDIR/pinsight_50_standby")"
    printf "  %-30s %s\n" "PInsight_50_MONITOR:" "$(get_trace_size "$TDIR/pinsight_50_monitor")"
    printf "  %-30s %s\n" "Nsight_Systems:"      "$(get_trace_size "$TDIR/lulesh_nsys"*.nsys-rep)"
    printf "  %-30s %s\n" "HPCToolkit:"          "$(get_trace_size "$TDIR/hpctoolkit-measurements")"
    printf "  %-30s %s\n" "Score-P:"             "$(get_trace_size "$TDIR/scorep_traces")"
    echo ""
done

echo "================================================================"
echo "E1 Evaluation Complete. $(date)"
echo "================================================================"
