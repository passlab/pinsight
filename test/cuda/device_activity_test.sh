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
# Locate libcupti rather than assuming the distro path.  Two reasons: CUDA 13
# moved it out of extras/CUPTI/lib64 into targets/, and blindly prepending
# /usr/lib/x86_64-linux-gnu to LD_LIBRARY_PATH can shadow an MPI's bundled
# libpmix with the distro one, which kills mpirun before the app ever starts
# ("undefined symbol: pmix_value_load") and shows up here as every mpirun arm
# silently reporting zero events.
cupti_default() {
    local c d
    for c in /usr/lib/x86_64-linux-gnu/libcupti.so \
             "${CUDA_PATH:-/usr/local/cuda}"/extras/CUPTI/lib64/libcupti.so \
             "${CUDA_PATH:-/usr/local/cuda}"/targets/x86_64-linux/lib/libcupti.so; do
        [ -e "$c" ] && { d=$(dirname "$c"); break; }
    done
    echo "${d:-/usr/lib/x86_64-linux-gnu}"
}
CUPTI_LIB_DIR=${CUPTI_LIB_DIR:-$(cupti_default)}
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

# mpirun flavor: Open MPI wants --oversubscribe and -x NAME=VALUE; MPICH/Hydra
# rejects both (its mpiexec aborts on --oversubscribe, so the multi-rank arms
# would report zero events for the wrong reason) and takes -genv NAME VALUE
# instead.  Neither approach preloads pinsight into mpirun itself — only the
# ranks get LD_PRELOAD.
MPI_FLAVOR=other
if command -v mpirun >/dev/null 2>&1 && \
   mpirun --version 2>&1 | grep -qiE 'open(-| )?mpi|open-?rte'; then
    MPI_FLAVOR=openmpi
fi

# mpi_launch <nranks> <cmd...> — run under mpirun with the pinsight env
# (LD_PRELOAD, PINSIGHT_TRACE_CONFIG_FILE, and the optional vars when set)
# forwarded to every rank in the launcher's own dialect.
mpi_launch() {
    local n="$1"; shift
    local -a envs=(LD_PRELOAD "$PINSIGHT_LIB" PINSIGHT_TRACE_CONFIG_FILE "$CFG")
    [ -n "${LD_LIBRARY_PATH:-}" ]         && envs+=(LD_LIBRARY_PATH "$LD_LIBRARY_PATH")
    [ -n "${PINSIGHT_DEBUG_ACTIVITY:-}" ] && envs+=(PINSIGHT_DEBUG_ACTIVITY "$PINSIGHT_DEBUG_ACTIVITY")
    [ -n "${MAPFILE:-}" ]                 && envs+=(MAPFILE "$MAPFILE")
    local -a flags=(); local i
    if [ "$MPI_FLAVOR" = openmpi ]; then
        for ((i=0; i<${#envs[@]}; i+=2)); do flags+=(-x "${envs[i]}=${envs[i+1]}"); done
        mpirun -np "$n" --oversubscribe "${flags[@]}" "$@"
    else
        for ((i=0; i<${#envs[@]}; i+=2)); do flags+=(-genv "${envs[i]}" "${envs[i+1]}"); done
        mpirun -np "$n" "${flags[@]}" "$@"
    fi
}

# run <policy> <nranks> [iters] [sleep_ms] -> populates $OUT, echoes nothing
run_arm() {
    local policy="$1" nranks="$2" iters="${3:-12}" sleep_ms="${4:-60}"
    mkcfg "$policy"
    lttng destroy -a >/dev/null 2>&1
    rm -rf "$OUT"
    lttng create act-cuda --output="$OUT" >/dev/null
    lttng enable-event -u 'cupti_pinsight_lttng_ust:*' >/dev/null
    lttng add-context -u -t vpid >/dev/null 2>&1
    lttng start >/dev/null
    if [ "$nranks" -gt 1 ]; then
        mpi_launch "$nranks" "$BIN" "$iters" "$sleep_ms" >/dev/null 2>"$OUT".applog
    else
        env LD_PRELOAD="$PINSIGHT_LIB" PINSIGHT_TRACE_CONFIG_FILE="$CFG" \
            "$BIN" "$iters" "$sleep_ms" >/dev/null 2>"$OUT".applog
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

echo "== A5: physical devId mapping under one-GPU-per-rank (needs >= 2 GPUs) =="
# Regression test for commit c260d34.  cuptiGetDeviceId() returns the ordinal
# RELATIVE to the process's CUDA_VISIBLE_DEVICES-masked view, so under a
# one-GPU-per-rank launcher EVERY rank sees ordinal 0 and the pre-fix trace
# collapsed all ranks onto devId=0.  cuda_parse_visible_devices() maps that
# ordinal back to the physical index.
#
# The assignment is deliberately REVERSED (rank r -> physical GPU N-1-r) so the
# map is non-identity.  That is what makes the arm decisive: it fails both for
# the original collapse bug (all devIds 0) AND for a naive devId=local_rank
# implementation (which would report the assignment backwards).
#
# NOTE: giving every rank the same full list (CUDA_VISIBLE_DEVICES=0,1,...)
# degenerates the map to identity and does NOT exercise this code path — see
# the topology caveat in pinsight-eval code-memory/matrix_castro_eval.md.
NGPU=$(nvidia-smi -L 2>/dev/null | grep -c '^GPU ')
if ! command -v mpirun >/dev/null 2>&1; then
    echo "  [SKIP] mpirun not found — needs >= 2 ranks"
elif [ "${NGPU:-0}" -lt 2 ]; then
    echo "  [SKIP] found ${NGPU:-0} GPU(s) — the ordinal->physical map is the"
    echo "         identity with one GPU, so this arm cannot distinguish a fix"
    echo "         from the bug it fixes"
else
    NDEV=$NGPU; [ "$NDEV" -gt 4 ] && NDEV=4   # keep the arm short on fat nodes
    MAPFILE=$(mktemp /tmp/devid_map.XXXX.txt); : > "$MAPFILE"
    WRAP=$(mktemp /tmp/devid_wrap.XXXX.sh)
    cat > "$WRAP" <<'WRAPEOF'
#!/bin/bash
# One distinct physical GPU per local rank, reversed: rank r -> GPU N-1-r.
# Open MPI and MPICH/Hydra publish local rank/size under different names.
r=${OMPI_COMM_WORLD_LOCAL_RANK:-${MPI_LOCALRANKID:-0}}
n=${OMPI_COMM_WORLD_LOCAL_SIZE:-${MPI_LOCALNRANKS:-1}}
export CUDA_VISIBLE_DEVICES=$(( n - 1 - r ))
echo "$$ $CUDA_VISIBLE_DEVICES" >> "$MAPFILE"   # pid -> expected physical devId
exec "$@"
WRAPEOF
    chmod +x "$WRAP"

    mkcfg on
    lttng destroy -a >/dev/null 2>&1
    rm -rf "$OUT"
    lttng create act-cuda --output="$OUT" >/dev/null
    lttng enable-event -u 'cupti_pinsight_lttng_ust:*' >/dev/null
    lttng add-context -u -t vpid >/dev/null 2>&1
    lttng start >/dev/null
    mpi_launch "$NDEV" "$WRAP" "$BIN" 12 60 >/dev/null 2>"$OUT".applog
    lttng stop >/dev/null; lttng destroy >/dev/null 2>&1

    # distinct devIds seen on a given event
    devids() { "$BT" "$OUT" 2>/dev/null | grep "$1" \
                 | grep -o 'devId = [0-9]*' | sort -u | wc -l; }
    # "<vpid> <devId>" pairs actually traced for a given event
    pairs()  { "$BT" "$OUT" 2>/dev/null | grep "$1" \
                 | grep -oE 'vpid = [0-9]+|devId = [0-9]+' | paste - - \
                 | sed 's/[^0-9]\+/ /g' | awk '{print $1" "$2}' | sort -u; }

    # Guard against a vacuous pass: the per-rank loop below iterates over
    # $MAPFILE, so an empty/short map would silently "pass" every check.
    check "wrapper recorded $NDEV rank->GPU assignments" "$(grep -c . "$MAPFILE")" "$NDEV"

    hv=$(vpids cudaKernelLaunch_begin)
    check "all $NDEV ranks emit host launches"        "$hv" "$NDEV"
    check "$NDEV distinct devIds on host launches"    "$(devids cudaKernelLaunch_begin)" "$NDEV"
    check "$NDEV distinct devIds on activity records" "$(devids cudaKernelActivity)"     "$NDEV"

    # Exact per-rank match: traced devId == the physical GPU that rank was given.
    for ev in cudaKernelLaunch_begin cudaKernelActivity; do
        bad=0
        while read -r pid want; do
            [ -n "$pid" ] || continue
            got=$(pairs "$ev" | awk -v p="$pid" '$1==p {print $2}')
            [ "$got" = "$want" ] || { bad=$((bad+1))
                echo "      pid $pid: traced devId '$got', assigned GPU '$want'"; }
        done < "$MAPFILE"
        check "$ev devId == assigned physical GPU" "$bad" 0
    done
    rm -f "$MAPFILE" "$WRAP"
fi

echo "== A6: device_activity = rotate_per_node -> turns taken, no scheduler needed =="
# Phase 2 (commit c205961), first NVIDIA coverage.  The collector is
# (mono_ms / period) % ranks_per_node == local_rank, so over a run spanning
# several periods EVERY local rank must take a turn, and the total activity
# count must be a fraction of the host launches (only one rank collects at a
# time) rather than equal to it.
#
# Deliberately run with NO scheduler env: topology comes from the launcher's
# own local-size variable (OMPI_COMM_WORLD_LOCAL_SIZE / MPI_LOCALNRANKS) via
# pinsight_ranks_per_node().  This arm is the regression guard for that — with
# only SLURM_NTASKS_PER_NODE consulted, a plain-mpirun run has unknown topology
# and silently degrades to leader_per_node, which shows up here as exactly one
# collector instead of $NRANKS.
#
# Rotation selects a RANK, not a device, so one GPU is sufficient.
ROT_MS=500
ROT_ITERS=40; ROT_SLEEP=100    # ~4 s of work == ~8 rotation periods
if ! command -v mpirun >/dev/null 2>&1; then
    echo "  [SKIP] mpirun not found — needs >= 2 ranks"
else
    ( unset SLURM_NTASKS_PER_NODE SLURM_LOCALID; \
      run_arm "rotate_per_node:$ROT_MS" "$NRANKS" "$ROT_ITERS" "$ROT_SLEEP" )
    la=$(count cudaKernelLaunch_begin); ac=$(count cudaKernelActivity)
    hv=$(vpids cudaKernelLaunch_begin); av=$(vpids cudaKernelActivity)
    check   "all $NRANKS ranks emit host launches"   "$hv" "$NRANKS"
    check   "every rank took a collection turn"      "$av" "$NRANKS"
    checkgt "rotation produced records"              "$ac" 0
    # One collector at a time: activity must be a fraction of total launches,
    # not all of them (which is what device_activity=on would give).
    if [ "$ac" -lt "$la" ]; then
        echo "  [PASS] only one rank collects at a time ($ac activity < $la launches)"
    else
        echo "  [FAIL] expected activity < launches, got $ac vs $la"; fails=$((fails+1))
    fi
    # Balance: no rank may hog the rotation (guards a stuck/degenerate clock).
    hog=$("$BT" "$OUT" 2>/dev/null | grep cudaKernelActivity \
            | grep -o 'vpid = [0-9]*' | sort | uniq -c | sort -rn \
            | awk -v t="$ac" 'NR==1 && t>0 {printf "%d", $1*100/t}')
    if [ -n "$hog" ] && [ "$hog" -le 80 ]; then
        echo "  [PASS] rotation balanced (busiest rank ${hog}% of records)"
    else
        echo "  [FAIL] rotation skewed: busiest rank ${hog:-?}% of $ac records"
        fails=$((fails+1))
    fi
fi

rm -f "$CFG"
echo ""
if [ "$fails" = 0 ]; then echo "ALL PASS"; else echo "$fails CHECK(S) FAILED"; exit 1; fi
