#!/bin/bash
# E1 Full LULESH Overhead Evaluation — Config-First Loop Order
#
# Strategy: Run each config across ALL thread counts before moving to next config.
#   1. Baseline         (8, 16, 24, 32, 48 threads × 5 runs each)
# Metrics: Elapsed, Grind, FOM, trace size
# Trace data deleted after measurement to save disk.
#
# Configurations (9):
#  1. Baseline
#  2. Baseline + TBB
#  3. PInsight Full Trace
#  4. PInsight Full Trace + TBB
#  5. PInsight 50→STANDBY
#  6. PInsight 50→STANDBY + TBB
#  7. HPCToolkit
#  8. Score-P  (may OOM)
#  9. Nsight Systems (may hang; timeout 300s)
#
# Output: results_60/e1_results.csv with columns:
#   threads,config,run,elapsed,grind,fom,trace_bytes,trace_human

set +e  # Don't abort on individual tool failures

# ============================================================
# Usage: ./run_e1_full_eval.sh [PROBLEM_SIZE] [THREAD_COUNTS] [NUM_RUNS]
#   PROBLEM_SIZE  : LULESH -s value (default: 60)
#   THREAD_COUNTS : quoted space-separated list (default: "8 16 24 32 48")
#   NUM_RUNS      : repetitions per config (default: 5)
#
# Examples:
#   ./run_e1_full_eval.sh                       # full eval: 60^3, 5 threads, 5 runs
#   ./run_e1_full_eval.sh 30 "8" 5              # smoke test: 30^3, 8 threads, 5 runs
#   ./run_e1_full_eval.sh 60 "8 16 32" 3        # partial: 60^3, 3 thread counts, 3 runs
# ============================================================

# ============================================================
# Paths and settings
# ============================================================
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
APP_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
SOURCE_DIR="$APP_DIR/source"
PINSIGHT_DIR="$(cd "$APP_DIR/../.." && pwd)"
PINSIGHT_LIB="$PINSIGHT_DIR/build/libpinsight.so"
# trace.sh no longer used; LTTng commands are inlined in run_pinsight_config
TBB_MALLOC="${TBB_MALLOC:-/home/yyan7/tools/hpctoolkit-install/lib/x86_64-linux-gnu/libtbbmalloc_proxy.so}"

S="${1:-60}"
THREAD_COUNTS="${2:-8 16 24 32 48}"
R="${3:-5}"

RESULTS_ROOT="$APP_DIR/results/$(hostname -s)"
RESULTS_DIR="$RESULTS_ROOT/results_${S}"
TRACE_DIR="$RESULTS_DIR/traces"
CSV_FILE="$RESULTS_DIR/e1_results.csv"
LOG_FILE="$RESULTS_DIR/e1_eval.log"
TMPLOG="$RESULTS_DIR/_tmp_run.log"  # temp file on /home, NOT /tmp

CFG_STANDBY="$SCRIPT_DIR/e1_trace_50_standby.txt"

LULESH="$SOURCE_DIR/lulesh2.0"
LULESH_SCOREP="$SOURCE_DIR/lulesh2.0_scorep"

export PATH=$HOME/tools/hpctoolkit-install/bin:$HOME/tools/scorep/bin:$PATH
export LD_LIBRARY_PATH=$HOME/tools/hpctoolkit-install/lib/x86_64-linux-gnu:$LD_LIBRARY_PATH

mkdir -p "$RESULTS_DIR" "$TRACE_DIR"
cd "$SOURCE_DIR"

# Build if needed
if [ ! -f "$LULESH" ]; then
    echo "Building LULESH..."
    make clean >/dev/null 2>&1 || true
    make -j20 >/dev/null 2>&1
fi
if [ ! -f "$LULESH_SCOREP" ]; then
    echo "Building LULESH with Score-P..."
    make clean >/dev/null 2>&1 || true
    make CXX="scorep g++ -DUSE_MPI=0" LULESH_EXEC=lulesh2.0_scorep LDFLAGS="-g -O3 -fopenmp" -j20 >/dev/null 2>&1
    make clean >/dev/null 2>&1 || true
    make -j20 >/dev/null 2>&1
fi

# ============================================================
# Helper: parse 3 metrics from a log file
# ============================================================
parse_metrics() {
    local f="$1"
    P_ELAPSED=$(awk '/Elapsed time/{for(i=1;i<=NF;i++) if($i=="="){printf "%.2f",$(i+1);break}}' "$f" 2>/dev/null)
    P_GRIND=$(awk '/Grind time/{for(i=1;i<=NF;i++) if($i=="="){printf "%.6f",$(i+1);break}}' "$f" 2>/dev/null)
    P_FOM=$(awk '/FOM/{for(i=1;i<=NF;i++) if($i=="="){printf "%.2f",$(i+1);break}}' "$f" 2>/dev/null)
}

get_trace_size() {
    local t="$1"
    if [ -d "$t/ust" ] || [ -e "$t" ]; then
        du -sb "$t" 2>/dev/null | cut -f1
    else
        echo "0"
    fi
}
get_trace_size_human() {
    local t="$1"
    if [ -d "$t/ust" ] || [ -e "$t" ]; then
        du -sh "$t" 2>/dev/null | cut -f1
    else
        echo "N/A"
    fi
}

# ============================================================
# Runner: run one config at one thread count, R repetitions
# ============================================================
# Usage: run_config CONFIG THREADS CMD...
#   Runs CMD R times, parses metrics, appends to CSV.
#   Console shows FOM per run.
run_config() {
    local config="$1"; shift
    local threads="$1"; shift
    local trace_dir_arg="$1"; shift   # trace dir or "-" if N/A
    # remaining args: the command to run (must end with LULESH args)

    printf "  T=%-4s" "$threads" | tee -a "$LOG_FILE"
    for i in $(seq 1 $R); do
        "$@" > "$TMPLOG" 2>&1 || true
        parse_metrics "$TMPLOG"

        local tbytes="" thuman=""
        if [ "$trace_dir_arg" != "-" ] && [ -n "$trace_dir_arg" ]; then
            tbytes=$(get_trace_size "$trace_dir_arg")
            thuman=$(get_trace_size_human "$trace_dir_arg")
            # Clean trace data after measurement
            rm -rf "$trace_dir_arg" 2>/dev/null
        fi

        if [ -z "$P_FOM" ]; then
            printf "%10s" "ERR" | tee -a "$LOG_FILE"
            echo "${threads},${config},${i},ERR,ERR,ERR,${tbytes},${thuman}" >> "$CSV_FILE"
        else
            printf "%10s" "$P_FOM" | tee -a "$LOG_FILE"
            echo "${threads},${config},${i},${P_ELAPSED},${P_GRIND},${P_FOM},${tbytes},${thuman}" >> "$CSV_FILE"
        fi
        rm -f "$TMPLOG"
    done
    echo "" | tee -a "$LOG_FILE"
}

# ============================================================
# PInsight runner: inline LTTng tracing (no trace.sh dependency)
# ============================================================
# Usage: run_pinsight_config CONFIG THREADS TRACE_BASE TRACE_NAME
# Note: Set extra env vars via `export` before calling.
run_pinsight_config() {
    local config="$1"; shift
    local threads="$1"; shift
    local trace_base="$1"; shift
    local trace_name="$1"; shift

    printf "  T=%-4s" "$threads" | tee -a "$LOG_FILE"
    for i in $(seq 1 $R); do
        local trace_dest="${trace_base}_run${i}"
        local sess_name="${trace_name}_r${i}-session"

        # ---- LTTng setup (quiet) ----
        lttng create "$sess_name" --output="$trace_dest" > /dev/null 2>&1 || true
        lttng enable-event --userspace 'pinsight_enter_exit_lttng_ust:*' > /dev/null 2>&1 || true
        lttng enable-event --userspace 'ompt_pinsight_lttng_ust:*' > /dev/null 2>&1 || true
        lttng enable-event --userspace 'pmpi_pinsight_lttng_ust:*' > /dev/null 2>&1 || true
        lttng enable-event --userspace 'cupti_pinsight_lttng_ust:*' > /dev/null 2>&1 || true
        lttng enable-event -u -a > /dev/null 2>&1 || true
        lttng add-context -u -t procname -t vpid -t vtid -t ip > /dev/null 2>&1 || true
        lttng start > /dev/null 2>&1 || true

        # ---- Run LULESH with PInsight, capture ONLY LULESH output ----
        # LD_PRELOAD combines PINSIGHT_LIB with any caller-exported LD_PRELOAD (e.g. TBB).
        env \
            LD_PRELOAD="${PINSIGHT_LIB} ${LD_PRELOAD}" \
            OMP_TOOL_LIBRARIES="${PINSIGHT_LIB}" \
            $LULESH -s $S > "$TMPLOG" 2>&1 || true

        # ---- LTTng teardown (quiet) ----
        lttng stop > /dev/null 2>&1 || true
        lttng destroy "$sess_name" > /dev/null 2>&1 || true

        parse_metrics "$TMPLOG"

        local tbytes=$(get_trace_size "$trace_dest")
        local thuman=$(get_trace_size_human "$trace_dest")

        if [ -z "$P_FOM" ]; then
            printf "%10s" "ERR" | tee -a "$LOG_FILE"
            echo "${threads},${config},${i},ERR,ERR,ERR,${tbytes},${thuman}" >> "$CSV_FILE"
        else
            printf "%10s" "$P_FOM" | tee -a "$LOG_FILE"
            echo "${threads},${config},${i},${P_ELAPSED},${P_GRIND},${P_FOM},${tbytes},${thuman}" >> "$CSV_FILE"
        fi
        # Clean trace data after measurement
        rm -rf "$trace_dest" 2>/dev/null
        rm -f "$TMPLOG"
    done
    echo "" | tee -a "$LOG_FILE"
}

# ============================================================
# Main
# ============================================================

echo "threads,config,run,elapsed,grind,fom,trace_bytes,trace_human" > "$CSV_FILE"

{
echo "================================================================"
echo "E1: LULESH 60^3 Overhead Evaluation (Config-First)"
echo "  Size=${S}, Runs=$R, Threads: $THREAD_COUNTS"
echo "  Date: $(date)"
echo "================================================================"
echo ""
} | tee "$LOG_FILE"

# ---- 1. Baseline ----
{
echo "──────────────────────────────────────────────────────"
echo "Config: Baseline"
echo "──────────────────────────────────────────────────────"
printf "  %-6s" ""; for i in $(seq 1 $R); do printf "%10s" "run$i"; done; echo ""
} | tee -a "$LOG_FILE"

for T in $THREAD_COUNTS; do
    export OMP_NUM_THREADS=$T
    run_config "Baseline" "$T" "-" env OMP_TOOL=disabled $LULESH -s $S
done

# ---- 2. Baseline + TBB ----
{
echo ""
echo "──────────────────────────────────────────────────────"
echo "Config: Baseline_TBB"
echo "──────────────────────────────────────────────────────"
printf "  %-6s" ""; for i in $(seq 1 $R); do printf "%10s" "run$i"; done; echo ""
} | tee -a "$LOG_FILE"

for T in $THREAD_COUNTS; do
    export OMP_NUM_THREADS=$T
    run_config "Baseline_TBB" "$T" "-" env OMP_TOOL=disabled LD_PRELOAD=$TBB_MALLOC $LULESH -s $S
done

# ---- 3. PInsight Full Trace ----
{
echo ""
echo "──────────────────────────────────────────────────────"
echo "Config: PInsight_Full"
echo "──────────────────────────────────────────────────────"
printf "  %-6s" ""; for i in $(seq 1 $R); do printf "%10s" "run$i"; done; echo ""
} | tee -a "$LOG_FILE"

for T in $THREAD_COUNTS; do
    export OMP_NUM_THREADS=$T
    TDIR="$TRACE_DIR/${T}t"
    mkdir -p "$TDIR"
    run_pinsight_config "PInsight_Full" "$T" "$TDIR/pinsight_full" "full_${T}t"
done

# ---- 4. PInsight Full Trace + TBB ----
{
echo ""
echo "──────────────────────────────────────────────────────"
echo "Config: PInsight_Full_TBB"
echo "──────────────────────────────────────────────────────"
printf "  %-6s" ""; for i in $(seq 1 $R); do printf "%10s" "run$i"; done; echo ""
} | tee -a "$LOG_FILE"

for T in $THREAD_COUNTS; do
    export OMP_NUM_THREADS=$T
    TDIR="$TRACE_DIR/${T}t"
    mkdir -p "$TDIR"
    export LD_PRELOAD=$TBB_MALLOC
    run_pinsight_config "PInsight_Full_TBB" "$T" "$TDIR/pinsight_full_tbb" "ftbb_${T}t"
    unset LD_PRELOAD
done

# ---- 5. PInsight 50→STANDBY ----
{
echo ""
echo "──────────────────────────────────────────────────────"
echo "Config: PInsight_50_STANDBY"
echo "──────────────────────────────────────────────────────"
printf "  %-6s" ""; for i in $(seq 1 $R); do printf "%10s" "run$i"; done; echo ""
} | tee -a "$LOG_FILE"

for T in $THREAD_COUNTS; do
    export OMP_NUM_THREADS=$T
    TDIR="$TRACE_DIR/${T}t"
    mkdir -p "$TDIR"
    export PINSIGHT_TRACE_CONFIG_FILE=$CFG_STANDBY
    run_pinsight_config "PInsight_50_STANDBY" "$T" "$TDIR/pinsight_standby" "sb_${T}t"
    unset PINSIGHT_TRACE_CONFIG_FILE
done

# ---- 6. PInsight 50→STANDBY + TBB ----
{
echo ""
echo "──────────────────────────────────────────────────────"
echo "Config: PInsight_50_STANDBY_TBB"
echo "──────────────────────────────────────────────────────"
printf "  %-6s" ""; for i in $(seq 1 $R); do printf "%10s" "run$i"; done; echo ""
} | tee -a "$LOG_FILE"

for T in $THREAD_COUNTS; do
    export OMP_NUM_THREADS=$T
    TDIR="$TRACE_DIR/${T}t"
    mkdir -p "$TDIR"
    export PINSIGHT_TRACE_CONFIG_FILE=$CFG_STANDBY
    export LD_PRELOAD=$TBB_MALLOC
    run_pinsight_config "PInsight_50_STANDBY_TBB" "$T" "$TDIR/pinsight_standby_tbb" "sbtbb_${T}t"
    unset PINSIGHT_TRACE_CONFIG_FILE LD_PRELOAD
done

# ---- 7. HPCToolkit ----
{
echo ""
echo "──────────────────────────────────────────────────────"
echo "Config: HPCToolkit"
echo "──────────────────────────────────────────────────────"
printf "  %-6s" ""; for i in $(seq 1 $R); do printf "%10s" "run$i"; done; echo ""
} | tee -a "$LOG_FILE"

for T in $THREAD_COUNTS; do
    export OMP_NUM_THREADS=$T
    TDIR="$TRACE_DIR/${T}t"
    mkdir -p "$TDIR"
    rm -rf "$TDIR/hpctoolkit"
    run_config "HPCToolkit" "$T" "$TDIR/hpctoolkit" \
        hpcrun -o "$TDIR/hpctoolkit" -e REALTIME@1000 -t $LULESH -s $S
done

# ---- 8. Score-P — DISABLED (OOM at 60^3) ----
# {
# echo ""
# echo "──────────────────────────────────────────────────────"
# echo "Config: ScoreP"
# echo "──────────────────────────────────────────────────────"
# printf "  %-6s" ""; for i in $(seq 1 $R); do printf "%10s" "run$i"; done; echo ""
# } | tee -a "$LOG_FILE"
#
# for T in $THREAD_COUNTS; do
#     export OMP_NUM_THREADS=$T
#     TDIR="$TRACE_DIR/${T}t"
#     mkdir -p "$TDIR"
#     export SCOREP_ENABLE_TRACING=true
#     export SCOREP_ENABLE_PROFILING=false
#     export SCOREP_TOTAL_MEMORY=4G
#     export SCOREP_EXPERIMENT_DIRECTORY="$TDIR/scorep_traces"
#     export SCOREP_OVERWRITE_EXPERIMENT_DIRECTORY=true
#     run_config "ScoreP" "$T" "$TDIR/scorep_traces" $LULESH_SCOREP -s $S
#     unset SCOREP_ENABLE_TRACING SCOREP_ENABLE_PROFILING SCOREP_TOTAL_MEMORY
#     unset SCOREP_EXPERIMENT_DIRECTORY SCOREP_OVERWRITE_EXPERIMENT_DIRECTORY
# done

# ---- 9. Nsight Systems — DISABLED (hangs in post-processing) ----
# {
# echo ""
# echo "──────────────────────────────────────────────────────"
# echo "Config: Nsight_Systems"
# echo "──────────────────────────────────────────────────────"
# printf "  %-6s" ""; for i in $(seq 1 $R); do printf "%10s" "run$i"; done; echo ""
# } | tee -a "$LOG_FILE"
#
# for T in $THREAD_COUNTS; do
#     export OMP_NUM_THREADS=$T
#     TDIR="$TRACE_DIR/${T}t"
#     mkdir -p "$TDIR"
#     run_config "Nsight_Systems" "$T" "$TDIR/lulesh_nsys.nsys-rep" \
#         timeout 300 nsys profile -f true --trace=openmp,osrt -o "$TDIR/lulesh_nsys" $LULESH -s $S
# done

# ---- Done ----
rm -f "$TMPLOG"
{
echo ""
echo "================================================================"
echo "E1 Complete. $(date)"
echo "Results: $CSV_FILE"
echo "================================================================"
} | tee -a "$LOG_FILE"
