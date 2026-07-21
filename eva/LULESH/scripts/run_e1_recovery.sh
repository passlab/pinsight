#!/bin/bash
# E1 Recovery — re-runs PInsight + remaining configs that failed
# FIX: use tee inside trace.sh call to capture LULESH output
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
# Helper functions
# ============================================================
parse_metrics() {
    # Parse from a given file instead of hardcoded /tmp path
    local logfile=$1
    PARSED_ELAPSED=$(awk '/Elapsed time/{for(i=1;i<=NF;i++) if($i=="=") {printf "%.2f", $(i+1); break}}' "$logfile" 2>/dev/null)
    PARSED_GRIND=$(awk '/Grind time/{for(i=1;i<=NF;i++) if($i=="=") {printf "%.6f", $(i+1); break}}' "$logfile" 2>/dev/null)
    PARSED_FOM=$(awk '/FOM/{for(i=1;i<=NF;i++) if($i=="=") {printf "%.2f", $(i+1); break}}' "$logfile" 2>/dev/null)
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

# run_cmd: runs command, captures output via tee
run_cmd() {
    local label=$1; shift
    printf "%-35s" "$label:" | tee -a "$LOG_FILE"
    for i in $(seq 1 $R); do
        local tmplog="/tmp/lulesh_e1_run${i}.log"
        "$@" -s $S > "$tmplog" 2>/dev/null || true
        parse_metrics "$tmplog"
        if [ -z "$PARSED_FOM" ]; then
            printf "%10s" "ERR" | tee -a "$LOG_FILE"
            echo "${CUR_THREADS},${label},${i},ERR,ERR,ERR,,," >> "$CSV_FILE"
        else
            printf "%10s" "$PARSED_FOM" | tee -a "$LOG_FILE"
            echo "${CUR_THREADS},${label},${i},${PARSED_ELAPSED},${PARSED_GRIND},${PARSED_FOM},,," >> "$CSV_FILE"
        fi
    done
    echo "" | tee -a "$LOG_FILE"
}

# run_pinsight: key fix — capture LULESH stdout separately via trace.sh pipe + tee
run_pinsight() {
    local label=$1; shift
    local trace_base=$1; shift
    local trace_name=$1; shift
    printf "%-35s" "$label:" | tee -a "$LOG_FILE"
    for i in $(seq 1 $R); do
        local trace_dest="${trace_base}_run${i}"
        local tmplog="/tmp/lulesh_e1_run${i}.log"
        # Run trace.sh and tee LULESH output to tmplog
        "$@" "$trace_dest" "${trace_name}" "$PINSIGHT_LIB" ":" \
            $LULESH -s $S 2>/dev/null | tee "$tmplog" > /dev/null || true
        parse_metrics "$tmplog"
        local tbytes=$(get_trace_size "$trace_dest")
        local thuman=$(get_trace_size_human "$trace_dest")
        if [ -z "$PARSED_FOM" ]; then
            printf "%10s" "ERR" | tee -a "$LOG_FILE"
            echo "${CUR_THREADS},${label},${i},ERR,ERR,ERR,${tbytes},${thuman}" >> "$CSV_FILE"
        else
            printf "%10s" "$PARSED_FOM" | tee -a "$LOG_FILE"
            echo "${CUR_THREADS},${label},${i},${PARSED_ELAPSED},${PARSED_GRIND},${PARSED_FOM},${tbytes},${thuman}" >> "$CSV_FILE"
        fi
    done
    echo "" | tee -a "$LOG_FILE"
}

# ============================================================
# Recovery run
# ============================================================

{
echo ""
echo "================================================================"
echo "E1 Recovery Run: $(date)"
echo "  Rerun: PInsight (16,24,32,48T), Baseline+HPCToolkit (24,32,48T)"
echo "================================================================"
} | tee -a "$LOG_FILE"

# ---- 16T: PInsight only (Baseline/HPCToolkit already good) ----
export OMP_NUM_THREADS=16
CUR_THREADS=16
TDIR="$TRACE_DIR/16t"
mkdir -p "$TDIR"
{
echo "=== OMP_NUM_THREADS=16 (PInsight recovery) ==="
printf "%-35s" "(FOM z/s)"; for i in $(seq 1 $R); do printf "%10s" "run$i"; done; echo ""
} | tee -a "$LOG_FILE"

run_pinsight "PInsight_Full" "$TDIR/pinsight_full" "lulesh_full_16" bash "$TRACE_SH"
run_pinsight "PInsight_Full_TBB" "$TDIR/pinsight_full_tbb" "lulesh_ftbb_16" \
    env LD_PRELOAD=$TBB_MALLOC bash "$TRACE_SH"
run_pinsight "PInsight_50_STANDBY" "$TDIR/pinsight_standby" "lulesh_sb_16" \
    env PINSIGHT_TRACE_CONFIG_FILE=$CFG_STANDBY bash "$TRACE_SH"
run_pinsight "PInsight_50_STANDBY_TBB" "$TDIR/pinsight_standby_tbb" "lulesh_sbtbb_16" \
    env PINSIGHT_TRACE_CONFIG_FILE=$CFG_STANDBY LD_PRELOAD=$TBB_MALLOC bash "$TRACE_SH"

# ---- 24T, 32T, 48T: Full run (all configs) ----
for T in 24 32 48; do
    export OMP_NUM_THREADS=$T
    CUR_THREADS=$T
    TDIR="$TRACE_DIR/${T}t"
    rm -rf "$TDIR"
    mkdir -p "$TDIR"
    {
    echo "================================================================"
    echo "=== OMP_NUM_THREADS=$T ==="
    echo "================================================================"
    printf "%-35s" "(FOM z/s)"; for i in $(seq 1 $R); do printf "%10s" "run$i"; done; echo ""
    echo "------------------------------------------------------------------------"
    } | tee -a "$LOG_FILE"

    run_cmd "Baseline" env OMP_TOOL=disabled $LULESH
    run_cmd "Baseline_TBB" env OMP_TOOL=disabled LD_PRELOAD=$TBB_MALLOC $LULESH

    export SCOREP_ENABLE_TRACING=true SCOREP_ENABLE_PROFILING=false SCOREP_TOTAL_MEMORY=4G
    export SCOREP_EXPERIMENT_DIRECTORY="$TDIR/scorep_traces" SCOREP_OVERWRITE_EXPERIMENT_DIRECTORY=true
    rm -rf "$TDIR/scorep_traces"
    run_cmd "ScoreP" $LULESH_SCOREP
    rm -rf "$TDIR/scorep_traces_tbb"
    export SCOREP_EXPERIMENT_DIRECTORY="$TDIR/scorep_traces_tbb"
    run_cmd "ScoreP_TBB" env LD_PRELOAD=$TBB_MALLOC $LULESH_SCOREP
    unset SCOREP_ENABLE_TRACING SCOREP_ENABLE_PROFILING SCOREP_TOTAL_MEMORY
    unset SCOREP_EXPERIMENT_DIRECTORY SCOREP_OVERWRITE_EXPERIMENT_DIRECTORY

    run_pinsight "PInsight_Full" "$TDIR/pinsight_full" "lulesh_full_${T}" bash "$TRACE_SH"
    run_pinsight "PInsight_Full_TBB" "$TDIR/pinsight_full_tbb" "lulesh_ftbb_${T}" \
        env LD_PRELOAD=$TBB_MALLOC bash "$TRACE_SH"
    run_pinsight "PInsight_50_STANDBY" "$TDIR/pinsight_standby" "lulesh_sb_${T}" \
        env PINSIGHT_TRACE_CONFIG_FILE=$CFG_STANDBY bash "$TRACE_SH"
    run_pinsight "PInsight_50_STANDBY_TBB" "$TDIR/pinsight_standby_tbb" "lulesh_sbtbb_${T}" \
        env PINSIGHT_TRACE_CONFIG_FILE=$CFG_STANDBY LD_PRELOAD=$TBB_MALLOC bash "$TRACE_SH"

    rm -rf "$TDIR/hpctoolkit"
    run_cmd "HPCToolkit" hpcrun -o "$TDIR/hpctoolkit" -e REALTIME@1000 -t $LULESH

    {
    echo ""
    echo "--- Trace Sizes (Thread=$T) ---"
    printf "  %-35s %s\n" "PInsight_Full:" "$(get_trace_size_human "$TDIR/pinsight_full_run${R}")"
    printf "  %-35s %s\n" "PInsight_50_STANDBY:" "$(get_trace_size_human "$TDIR/pinsight_standby_run${R}")"
    printf "  %-35s %s\n" "HPCToolkit:" "$(get_trace_size_human "$TDIR/hpctoolkit")"
    echo ""
    } | tee -a "$LOG_FILE"
done

{
echo "================================================================"
echo "E1 Recovery Complete. $(date)"
echo "================================================================"
} | tee -a "$LOG_FILE"
