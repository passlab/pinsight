#!/bin/bash
# device_activity node-policy gate test for the CUDA/CUPTI domain.
#
# This is the automated form of the hand-run matrix smoke (job 288097) that
# HW-validated the NVIDIA gate: it asserts that `[CUDA.default] device_activity`
# controls ONLY the CUPTI activity collection, never host-side CUDA tracing.
#
# Arms:
#   off             -> 0 cudaKernelActivity, host cudaKernelLaunch_begin still > 0
#   on              -> cudaKernelActivity > 0 (and == launches)
#   leader_per_node -> with N ranks on this node, exactly ONE vpid emits activity,
#                      while ALL ranks still emit host launches
#
# The leader arm needs >= 2 ranks; it uses mpirun, which exports
# OMPI_COMM_WORLD_LOCAL_RANK — one of the env sources
# trace_config.c:local_rank_from_env() consults for the election.  A single GPU is
# fine: the ranks share it, and the assertion is about WHICH rank collects, not
# about device count.  Skipped automatically if mpirun is unavailable.
#
# Run with PINSIGHT_DEBUG_ACTIVITY=1 to see the gate decisions and CUPTI codes.
#
# Usage: ./device_activity_test.sh   (env: PINSIGHT_LIB, CUPTI_LIB_DIR, LTTNG_HOME, NRANKS)
set -u
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
HERE=$(cd "$(dirname "$0")" && pwd)
PINSIGHT_LIB=${PINSIGHT_LIB:-$ROOT/build_cuda/libpinsight.so}
CUPTI_LIB_DIR=${CUPTI_LIB_DIR:-/usr/lib/x86_64-linux-gnu}
BIN=$HERE/looping_pinsight
OUT=${OUT:-$HERE/activity_traces}
NRANKS=${NRANKS:-2}
CFG=$(mktemp /tmp/act_cuda_cfg.XXXX.txt)
if [ -n "${LTTNG_HOME:-}" ]; then
    export PATH=$LTTNG_HOME/bin:$PATH
    export LD_LIBRARY_PATH=$LTTNG_HOME/lib:$CUPTI_LIB_DIR:${LD_LIBRARY_PATH:-}
else
    export LD_LIBRARY_PATH=$CUPTI_LIB_DIR:${LD_LIBRARY_PATH:-}
fi

# babeltrace2 (LTTng 2.x reader) preferred; fall back to babeltrace 1.x, which
# reads the same CTF traces.  Without this a missing reader silently yields
# zero counts and every assertion "fails" for the wrong reason.
BT=$(command -v babeltrace2 || command -v babeltrace) || {
    echo "ERROR: neither babeltrace2 nor babeltrace found in PATH" >&2; exit 1; }
[ -x "$BIN" ] || { echo "build first:  make looping_pinsight"; exit 1; }
[ -e "$PINSIGHT_LIB" ] || { echo "missing $PINSIGHT_LIB (build with PINSIGHT_CUDA=TRUE)"; exit 1; }

fails=0
lttng-sessiond --daemonize >/dev/null 2>&1; sleep 0.3

mkcfg() { printf '[CUDA.default]\n    trace_mode = TRACING\n    device_activity = %s\n' "$1" > "$CFG"; }

# run <policy> <nranks> -> populates $OUT, echoes nothing
run_arm() {
    local policy="$1" nranks="$2"
    mkcfg "$policy"
    lttng destroy -a >/dev/null 2>&1
    rm -rf "$OUT"
    lttng create act-cuda --output="$OUT" >/dev/null
    lttng enable-event -u 'cupti_pinsight_lttng_ust:*' >/dev/null
    lttng add-context -u -t vpid >/dev/null 2>&1
    lttng start >/dev/null
    if [ "$nranks" -gt 1 ]; then
        mpirun -np "$nranks" --oversubscribe \
            -x LD_PRELOAD="$PINSIGHT_LIB" -x PINSIGHT_TRACE_CONFIG_FILE="$CFG" \
            -x LD_LIBRARY_PATH -x PINSIGHT_DEBUG_ACTIVITY \
            "$BIN" 12 60 >/dev/null 2>"$OUT".applog
    else
        env LD_PRELOAD="$PINSIGHT_LIB" PINSIGHT_TRACE_CONFIG_FILE="$CFG" \
            "$BIN" 12 60 >/dev/null 2>"$OUT".applog
    fi
    lttng stop >/dev/null; lttng destroy >/dev/null 2>&1
}

count()  { "$BT" "$OUT" 2>/dev/null | grep -c "$1"; }
# distinct vpids that emitted a given event
vpids()  { "$BT" "$OUT" 2>/dev/null | grep "$1" \
             | grep -o 'vpid = [0-9]*' | sort -u | wc -l; }
check()  { if [ "$2" = "$3" ]; then echo "  [PASS] $1 ($2)"; \
           else echo "  [FAIL] $1: got '$2' expected '$3'"; fails=$((fails+1)); fi; }
checkgt(){ if [ "$2" -gt "$3" ]; then echo "  [PASS] $1 ($2 > $3)"; \
           else echo "  [FAIL] $1: got '$2' expected > '$3'"; fails=$((fails+1)); fi; }

echo "== A1: device_activity = off -> no activity records, host tracing intact =="
run_arm off 1
la=$(count cudaKernelLaunch_begin); ac=$(count cudaKernelActivity)
checkgt "host launches still traced" "$la" 0
check   "activity records suppressed" "$ac" 0

echo "== A2: device_activity = on -> activity records present and matched =="
run_arm on 1
la=$(count cudaKernelLaunch_begin); ac=$(count cudaKernelActivity)
checkgt "host launches traced"     "$la" 0
checkgt "activity records present" "$ac" 0
check   "activity == launches"     "$ac" "$la"

echo "== A3: device_activity = leader_per_node -> exactly one collecting rank =="
if ! command -v mpirun >/dev/null 2>&1; then
    echo "  [SKIP] mpirun not found — leader election needs >= 2 ranks"
else
    run_arm leader_per_node "$NRANKS"
    hv=$(vpids cudaKernelLaunch_begin); av=$(vpids cudaKernelActivity)
    ac=$(count cudaKernelActivity)
    check   "all $NRANKS ranks emit host launches" "$hv" "$NRANKS"
    check   "exactly 1 rank collects activity"     "$av" 1
    checkgt "leader actually produced records"     "$ac" 0
fi

echo "== A4: device_activity = anyone_per_node -> exactly one collecting rank =="
# Different election path from A3: no rank-0 assumption — claim_node_singleton()
# flocks /tmp/$USER/pinsight_cuda_activity_singleton.lock and exactly one process
# per node wins.  Exercisable with 1 GPU: the policy selects a RANK, not a device.
if ! command -v mpirun >/dev/null 2>&1; then
    echo "  [SKIP] mpirun not found — needs >= 2 ranks"
else
    run_arm anyone_per_node "$NRANKS"
    hv=$(vpids cudaKernelLaunch_begin); av=$(vpids cudaKernelActivity)
    ac=$(count cudaKernelActivity)
    check   "all $NRANKS ranks emit host launches" "$hv" "$NRANKS"
    check   "exactly 1 rank collects activity"     "$av" 1
    checkgt "winner actually produced records"     "$ac" 0
fi

rm -f "$CFG"
echo ""
if [ "$fails" = 0 ]; then echo "ALL PASS"; else echo "$fails CHECK(S) FAILED"; exit 1; fi
