#!/bin/bash
# E1 Continuation — picks up from 8T HPCToolkit onward
# Nsight Systems DISABLED (hangs at 60^3 multi-threaded)

set +e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
APP_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
SOURCE_DIR="$APP_DIR/source"
PINSIGHT_DIR="$(cd "$APP_DIR/../.." && pwd)"
PINSIGHT_LIB="$PINSIGHT_DIR/build/libpinsight.so"
TRACE_SH="$PINSIGHT_DIR/scripts/trace.sh"
TBB_MALLOC="${TBB_MALLOC:-/home/yyan7/tools/hpctoolkit-install/lib/x86_64-linux-gnu/libtbbmalloc_proxy.so}"

S="60"
R="5"

RESULTS_ROOT="$APP_DIR/results/$(hostname -s)"
RESULTS_DIR="$RESULTS_ROOT/results_60"
TRACE_DIR="$RESULTS_DIR/traces"
CSV_FILE="$RESULTS_DIR/e1_results.csv"
LOG_FILE="$RESULTS_DIR/e1_eval.log"
mkdir -p "$RESULTS_DIR"

CFG_STANDBY="$SCRIPT_DIR/e1_trace_50_standby.txt"

LULESH="$SOURCE_DIR/lulesh2.0"
LULESH_SCOREP="$SOURCE_DIR/lulesh2.0_scorep"

export PATH=$HOME/tools/hpctoolkit-install/bin:$HOME/tools/scorep/bin:$PATH
export LD_LIBRARY_PATH=$HOME/tools/hpctoolkit-install/lib/x86_64-linux-gnu:$LD_LIBRARY_PATH

# ============================================================
# Helper functions (same as main script)
# ============================================================
parse_elapsed() {
    awk '/Elapsed time/{for(i=1;i<=NF;i++) if($i=="=") {printf "%.2f", $(i+1); break}}' /tmp/lulesh_e1.log 2>/dev/null
}
parse_grind() {
    awk '/Grind time/{for(i=1;i<=NF;i++) if($i=="=") {printf "%.6f", $(i+1); break}}' /tmp/lulesh_e1.log 2>/dev/null
}
parse_fom() {
    awk '/FOM/{for(i=1;i<=NF;i++) if($i=="=") {printf "%.2f", $(i+1); break}}' /tmp/lulesh_e1.log 2>/dev/null
}
get_trace_size() {
    local target="$1"
    if [ -d "$target/ust" ]; then du -sb "$target" 2>/dev/null | cut -f1
    elif [ -e "$target" ]; then du -sb "$target" 2>/dev/null | cut -f1
    else echo "0"; fi
}
get_trace_size_human() {
    local target="$1"
    if [ -d "$target/ust" ]; then du -sh "$target" 2>/dev/null | cut -f1
    elif [ -e "$target" ]; then du -sh "$target" 2>/dev/null | cut -f1
    else echo "N/A"; fi
}

run_cmd() {
    local label=$1; shift
    printf "%-35s" "$label:" | tee -a "$LOG_FILE"
    for i in $(seq 1 $R); do
        "$@" -s $S >/tmp/lulesh_e1.log 2>/tmp/lulesh_e1_err.log || true
        local elapsed=$(parse_elapsed)
        local grind=$(parse_grind)
        local fom=$(parse_fom)
        if [ -z "$fom" ]; then
            printf "%10s" "ERR" | tee -a "$LOG_FILE"
            echo "${CUR_THREADS},${label},${i},ERR,ERR,ERR,,," >> "$CSV_FILE"
        else
            printf "%10s" "$fom" | tee -a "$LOG_FILE"
            echo "${CUR_THREADS},${label},${i},${elapsed},${grind},${fom},,," >> "$CSV_FILE"
        fi
    done
    echo "" | tee -a "$LOG_FILE"
}

run_pinsight() {
    local label=$1; shift
    local trace_base=$1; shift
    local trace_name=$1; shift
    printf "%-35s" "$label:" | tee -a "$LOG_FILE"
    for i in $(seq 1 $R); do
        local trace_dest="${trace_base}_run${i}"
        "$@" "$trace_dest" "$trace_name" "$PINSIGHT_LIB" ":" \
            $LULESH -s $S >/tmp/lulesh_e1.log 2>/tmp/lulesh_e1_err.log || true
        local elapsed=$(parse_elapsed)
        local grind=$(parse_grind)
        local fom=$(parse_fom)
        local tbytes=$(get_trace_size "$trace_dest")
        local thuman=$(get_trace_size_human "$trace_dest")
        if [ -z "$fom" ]; then
            printf "%10s" "ERR" | tee -a "$LOG_FILE"
            echo "${CUR_THREADS},${label},${i},ERR,ERR,ERR,${tbytes},${thuman}" >> "$CSV_FILE"
        else
            printf "%10s" "$fom" | tee -a "$LOG_FILE"
            echo "${CUR_THREADS},${label},${i},${elapsed},${grind},${fom},${tbytes},${thuman}" >> "$CSV_FILE"
        fi
    done
    echo "" | tee -a "$LOG_FILE"
}

# ============================================================
# Continuation: 8T HPCToolkit + all remaining thread counts
# ============================================================

{
echo ""
echo "================================================================"
echo "E1 Continuation: $(date)"
echo "================================================================"
} | tee -a "$LOG_FILE"

# ---- Finish 8T: HPCToolkit only ----
export OMP_NUM_THREADS=8
CUR_THREADS=8
TDIR="$TRACE_DIR/8t"
mkdir -p "$TDIR"

{
echo "=== Finishing OMP_NUM_THREADS=8 (HPCToolkit) ==="
printf "%-35s" "(FOM z/s)"; for i in $(seq 1 $R); do printf "%10s" "run$i"; done; echo ""
} | tee -a "$LOG_FILE"

rm -rf "$TDIR/hpctoolkit"
run_cmd "HPCToolkit" hpcrun -o "$TDIR/hpctoolkit" -e REALTIME@1000 -t $LULESH

# ---- Remaining thread counts: 16, 24, 32, 48 ----
for T in 16 24 32 48; do
    export OMP_NUM_THREADS=$T
    CUR_THREADS=$T

    {
    echo "================================================================"
    echo "=== OMP_NUM_THREADS=$T ==="
    echo "================================================================"
    printf "%-35s" "(FOM z/s)"; for i in $(seq 1 $R); do printf "%10s" "run$i"; done; echo ""
    echo "------------------------------------------------------------------------"
    } | tee -a "$LOG_FILE"

    TDIR="$TRACE_DIR/${T}t"
    rm -rf "$TDIR"
    mkdir -p "$TDIR"

    # 1. Baseline
    run_cmd "Baseline" env OMP_TOOL=disabled $LULESH
    # 2. Baseline + TBB
    run_cmd "Baseline_TBB" env OMP_TOOL=disabled LD_PRELOAD=$TBB_MALLOC $LULESH
    # 3. Score-P
    export SCOREP_ENABLE_TRACING=true SCOREP_ENABLE_PROFILING=false SCOREP_TOTAL_MEMORY=4G
    export SCOREP_EXPERIMENT_DIRECTORY="$TDIR/scorep_traces" SCOREP_OVERWRITE_EXPERIMENT_DIRECTORY=true
    rm -rf "$TDIR/scorep_traces"
    run_cmd "ScoreP" $LULESH_SCOREP
    # 4. Score-P + TBB
    rm -rf "$TDIR/scorep_traces_tbb"
    export SCOREP_EXPERIMENT_DIRECTORY="$TDIR/scorep_traces_tbb"
    run_cmd "ScoreP_TBB" env LD_PRELOAD=$TBB_MALLOC $LULESH_SCOREP
    unset SCOREP_ENABLE_TRACING SCOREP_ENABLE_PROFILING SCOREP_TOTAL_MEMORY
    unset SCOREP_EXPERIMENT_DIRECTORY SCOREP_OVERWRITE_EXPERIMENT_DIRECTORY
    # 5. PInsight Full
    run_pinsight "PInsight_Full" "$TDIR/pinsight_full" "lulesh_full" bash "$TRACE_SH"
    # 6. PInsight Full + TBB
    run_pinsight "PInsight_Full_TBB" "$TDIR/pinsight_full_tbb" "lulesh_full_tbb" \
        env LD_PRELOAD=$TBB_MALLOC bash "$TRACE_SH"
    # 7. PInsight 50->STANDBY
    run_pinsight "PInsight_50_STANDBY" "$TDIR/pinsight_standby" "lulesh_standby" \
        env PINSIGHT_TRACE_CONFIG_FILE=$CFG_STANDBY bash "$TRACE_SH"
    # 8. PInsight 50->STANDBY + TBB
    run_pinsight "PInsight_50_STANDBY_TBB" "$TDIR/pinsight_standby_tbb" "lulesh_standby_tbb" \
        env PINSIGHT_TRACE_CONFIG_FILE=$CFG_STANDBY LD_PRELOAD=$TBB_MALLOC bash "$TRACE_SH"
    # 9. HPCToolkit
    rm -rf "$TDIR/hpctoolkit"
    run_cmd "HPCToolkit" hpcrun -o "$TDIR/hpctoolkit" -e REALTIME@1000 -t $LULESH

    # Trace sizes
    {
    echo ""
    echo "--- Trace Sizes (Thread=$T) ---"
    printf "  %-35s %s\n" "PInsight_Full:" "$(get_trace_size_human "$TDIR/pinsight_full_run${R}")"
    printf "  %-35s %s\n" "PInsight_Full_TBB:" "$(get_trace_size_human "$TDIR/pinsight_full_tbb_run${R}")"
    printf "  %-35s %s\n" "PInsight_50_STANDBY:" "$(get_trace_size_human "$TDIR/pinsight_standby_run${R}")"
    printf "  %-35s %s\n" "PInsight_50_STANDBY_TBB:" "$(get_trace_size_human "$TDIR/pinsight_standby_tbb_run${R}")"
    printf "  %-35s %s\n" "HPCToolkit:" "$(get_trace_size_human "$TDIR/hpctoolkit")"
    echo ""
    } | tee -a "$LOG_FILE"
done

{
echo "================================================================"
echo "E1 Continuation Complete. $(date)"
echo "================================================================"
} | tee -a "$LOG_FILE"
