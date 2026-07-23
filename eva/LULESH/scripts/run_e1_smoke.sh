#!/bin/bash
# Smoke test: S=30, T=8, R=5, all 9 configs
set +e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
APP_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
SOURCE_DIR="$APP_DIR/source"
PINSIGHT_DIR="$(cd "$APP_DIR/../.." && pwd)"
cd "$SOURCE_DIR"
export OMP_NUM_THREADS=8
export PATH=$HOME/tools/hpctoolkit-install/bin:$HOME/tools/scorep/bin:$PATH
export LD_LIBRARY_PATH=$HOME/tools/hpctoolkit-install/lib/x86_64-linux-gnu:$LD_LIBRARY_PATH

PINSIGHT_LIB="$PINSIGHT_DIR/build/libpinsight.so"
TBB=$HOME/tools/hpctoolkit-install/lib/x86_64-linux-gnu/libtbbmalloc_proxy.so
CFG="$SCRIPT_DIR/e1_trace_50_standby.txt"
L="$SOURCE_DIR/lulesh2.0"
LS="$SOURCE_DIR/lulesh2.0_scorep"
S=30
R=5
TD=/tmp/smoke_traces
TMP=/tmp/smoke_tmp.log
CSV=/tmp/smoke_results.csv

mkdir -p $TD
echo "threads,config,run,elapsed,grind,fom,trace_bytes,trace_human" > $CSV

parse_all() {
    local f=$1
    local e=$(awk '/Elapsed time/{for(i=1;i<=NF;i++) if($i=="="){printf "%.2f",$(i+1);break}}' "$f" 2>/dev/null)
    local g=$(awk '/Grind time/{for(i=1;i<=NF;i++) if($i=="="){printf "%.6f",$(i+1);break}}' "$f" 2>/dev/null)
    local fom=$(awk '/FOM/{for(i=1;i<=NF;i++) if($i=="="){printf "%.2f",$(i+1);break}}' "$f" 2>/dev/null)
    echo "${e:-ERR},${g:-ERR},${fom:-ERR}"
}

echo "=== Smoke Test: S=$S, T=8, R=$R ==="
echo ""

# --- Baseline ---
printf "%-30s" "Baseline:"
for i in $(seq 1 $R); do
    env OMP_TOOL=disabled $L -s $S > $TMP 2>&1 || true
    IFS=',' read e g f <<< "$(parse_all $TMP)"
    printf "%10s" "$f"
    echo "8,Baseline,$i,$e,$g,$f,," >> $CSV
    rm -f $TMP
done
echo ""

# --- Baseline_TBB ---
printf "%-30s" "Baseline_TBB:"
for i in $(seq 1 $R); do
    env OMP_TOOL=disabled LD_PRELOAD=$TBB $L -s $S > $TMP 2>&1 || true
    IFS=',' read e g f <<< "$(parse_all $TMP)"
    printf "%10s" "$f"
    echo "8,Baseline_TBB,$i,$e,$g,$f,," >> $CSV
    rm -f $TMP
done
echo ""

# --- PInsight_Full ---
printf "%-30s" "PInsight_Full:"
for i in $(seq 1 $R); do
    td="$TD/pf_r${i}"; sn="pf_r${i}-s"
    lttng create "$sn" --output="$td" >/dev/null 2>&1 || true
    lttng enable-event --userspace 'pinsight_enter_exit_lttng_ust:*' >/dev/null 2>&1 || true
    lttng enable-event --userspace 'ompt_pinsight_lttng_ust:*' >/dev/null 2>&1 || true
    lttng enable-event -u -a >/dev/null 2>&1 || true
    lttng add-context -u -t procname -t vpid -t vtid -t ip >/dev/null 2>&1 || true
    lttng start >/dev/null 2>&1 || true
    env LD_PRELOAD="$PINSIGHT_LIB" OMP_TOOL_LIBRARIES="$PINSIGHT_LIB" $L -s $S > $TMP 2>&1 || true
    lttng stop >/dev/null 2>&1 || true; lttng destroy "$sn" >/dev/null 2>&1 || true
    IFS=',' read e g f <<< "$(parse_all $TMP)"
    tb=$(du -sb "$td" 2>/dev/null | cut -f1); [ -z "$tb" ] && tb=0
    th=$(du -sh "$td" 2>/dev/null | cut -f1); [ -z "$th" ] && th="N/A"
    printf "%10s" "$f"
    echo "8,PInsight_Full,$i,$e,$g,$f,$tb,$th" >> $CSV
    rm -rf "$td" $TMP
done
echo ""

# --- PInsight_Full_TBB ---
printf "%-30s" "PInsight_Full_TBB:"
for i in $(seq 1 $R); do
    td="$TD/pft_r${i}"; sn="pft_r${i}-s"
    lttng create "$sn" --output="$td" >/dev/null 2>&1 || true
    lttng enable-event --userspace 'pinsight_enter_exit_lttng_ust:*' >/dev/null 2>&1 || true
    lttng enable-event --userspace 'ompt_pinsight_lttng_ust:*' >/dev/null 2>&1 || true
    lttng enable-event -u -a >/dev/null 2>&1 || true
    lttng add-context -u -t procname -t vpid -t vtid -t ip >/dev/null 2>&1 || true
    lttng start >/dev/null 2>&1 || true
    env LD_PRELOAD="$PINSIGHT_LIB $TBB" OMP_TOOL_LIBRARIES="$PINSIGHT_LIB" $L -s $S > $TMP 2>&1 || true
    lttng stop >/dev/null 2>&1 || true; lttng destroy "$sn" >/dev/null 2>&1 || true
    IFS=',' read e g f <<< "$(parse_all $TMP)"
    tb=$(du -sb "$td" 2>/dev/null | cut -f1); [ -z "$tb" ] && tb=0
    th=$(du -sh "$td" 2>/dev/null | cut -f1); [ -z "$th" ] && th="N/A"
    printf "%10s" "$f"
    echo "8,PInsight_Full_TBB,$i,$e,$g,$f,$tb,$th" >> $CSV
    rm -rf "$td" $TMP
done
echo ""

# --- PInsight_50_STANDBY ---
printf "%-30s" "PInsight_50_STANDBY:"
for i in $(seq 1 $R); do
    td="$TD/ps_r${i}"; sn="ps_r${i}-s"
    lttng create "$sn" --output="$td" >/dev/null 2>&1 || true
    lttng enable-event --userspace 'pinsight_enter_exit_lttng_ust:*' >/dev/null 2>&1 || true
    lttng enable-event --userspace 'ompt_pinsight_lttng_ust:*' >/dev/null 2>&1 || true
    lttng enable-event -u -a >/dev/null 2>&1 || true
    lttng add-context -u -t procname -t vpid -t vtid -t ip >/dev/null 2>&1 || true
    lttng start >/dev/null 2>&1 || true
    env PINSIGHT_TRACE_CONFIG_FILE=$CFG LD_PRELOAD="$PINSIGHT_LIB" OMP_TOOL_LIBRARIES="$PINSIGHT_LIB" $L -s $S > $TMP 2>&1 || true
    lttng stop >/dev/null 2>&1 || true; lttng destroy "$sn" >/dev/null 2>&1 || true
    IFS=',' read e g f <<< "$(parse_all $TMP)"
    tb=$(du -sb "$td" 2>/dev/null | cut -f1); [ -z "$tb" ] && tb=0
    th=$(du -sh "$td" 2>/dev/null | cut -f1); [ -z "$th" ] && th="N/A"
    printf "%10s" "$f"
    echo "8,PInsight_50_STANDBY,$i,$e,$g,$f,$tb,$th" >> $CSV
    rm -rf "$td" $TMP
done
echo ""

# --- PInsight_50_STANDBY_TBB ---
printf "%-30s" "PInsight_50_STANDBY_TBB:"
for i in $(seq 1 $R); do
    td="$TD/pst_r${i}"; sn="pst_r${i}-s"
    lttng create "$sn" --output="$td" >/dev/null 2>&1 || true
    lttng enable-event --userspace 'pinsight_enter_exit_lttng_ust:*' >/dev/null 2>&1 || true
    lttng enable-event --userspace 'ompt_pinsight_lttng_ust:*' >/dev/null 2>&1 || true
    lttng enable-event -u -a >/dev/null 2>&1 || true
    lttng add-context -u -t procname -t vpid -t vtid -t ip >/dev/null 2>&1 || true
    lttng start >/dev/null 2>&1 || true
    env PINSIGHT_TRACE_CONFIG_FILE=$CFG LD_PRELOAD="$PINSIGHT_LIB $TBB" OMP_TOOL_LIBRARIES="$PINSIGHT_LIB" $L -s $S > $TMP 2>&1 || true
    lttng stop >/dev/null 2>&1 || true; lttng destroy "$sn" >/dev/null 2>&1 || true
    IFS=',' read e g f <<< "$(parse_all $TMP)"
    tb=$(du -sb "$td" 2>/dev/null | cut -f1); [ -z "$tb" ] && tb=0
    th=$(du -sh "$td" 2>/dev/null | cut -f1); [ -z "$th" ] && th="N/A"
    printf "%10s" "$f"
    echo "8,PInsight_50_STANDBY_TBB,$i,$e,$g,$f,$tb,$th" >> $CSV
    rm -rf "$td" $TMP
done
echo ""

# --- HPCToolkit ---
printf "%-30s" "HPCToolkit:"
for i in $(seq 1 $R); do
    rm -rf $TD/hpc
    hpcrun -o $TD/hpc -e REALTIME@1000 -t $L -s $S > $TMP 2>&1 || true
    IFS=',' read e g f <<< "$(parse_all $TMP)"
    tb=$(du -sb "$TD/hpc" 2>/dev/null | cut -f1); [ -z "$tb" ] && tb=0
    th=$(du -sh "$TD/hpc" 2>/dev/null | cut -f1); [ -z "$th" ] && th="N/A"
    printf "%10s" "$f"
    echo "8,HPCToolkit,$i,$e,$g,$f,$tb,$th" >> $CSV
    rm -rf $TD/hpc $TMP
done
echo ""

# --- ScoreP ---
printf "%-30s" "ScoreP:"
export SCOREP_ENABLE_TRACING=true SCOREP_ENABLE_PROFILING=false SCOREP_TOTAL_MEMORY=4G
export SCOREP_EXPERIMENT_DIRECTORY=$TD/scorep SCOREP_OVERWRITE_EXPERIMENT_DIRECTORY=true
for i in $(seq 1 $R); do
    rm -rf $TD/scorep
    $LS -s $S > $TMP 2>&1 || true
    IFS=',' read e g f <<< "$(parse_all $TMP)"
    tb=$(du -sb "$TD/scorep" 2>/dev/null | cut -f1); [ -z "$tb" ] && tb=0
    th=$(du -sh "$TD/scorep" 2>/dev/null | cut -f1); [ -z "$th" ] && th="N/A"
    printf "%10s" "$f"
    echo "8,ScoreP,$i,$e,$g,$f,$tb,$th" >> $CSV
    rm -rf $TD/scorep $TMP
done
unset SCOREP_ENABLE_TRACING SCOREP_ENABLE_PROFILING SCOREP_TOTAL_MEMORY SCOREP_EXPERIMENT_DIRECTORY SCOREP_OVERWRITE_EXPERIMENT_DIRECTORY
echo ""

# --- Nsight_Systems ---
printf "%-30s" "Nsight_Systems:"
for i in $(seq 1 $R); do
    rm -f $TD/nsys.nsys-rep
    timeout 120 nsys profile -f true --trace=openmp,osrt -o $TD/nsys $L -s $S > $TMP 2>&1 || true
    IFS=',' read e g f <<< "$(parse_all $TMP)"
    tb=$(du -sb "$TD/nsys.nsys-rep" 2>/dev/null | cut -f1); [ -z "$tb" ] && tb=0
    th=$(du -sh "$TD/nsys.nsys-rep" 2>/dev/null | cut -f1); [ -z "$th" ] && th="N/A"
    printf "%10s" "$f"
    echo "8,Nsight_Systems,$i,$e,$g,$f,$tb,$th" >> $CSV
    rm -f $TD/nsys.nsys-rep $TMP
done
echo ""

echo ""
echo "=== CSV Results ==="
column -t -s, $CSV
