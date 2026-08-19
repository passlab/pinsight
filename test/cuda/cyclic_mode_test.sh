#!/bin/bash
# Cyclic runtime-mode test for the CUDA/CUPTI activity collection path.
# CUDA counterpart of test/rocm/cyclic_mode_test.sh.
#
# Drives looping_pinsight through TRACING -> MONITORING -> TRACING -> MONITORING by
# rewriting the trace-config file and sending SIGUSR1 (PInsight's config-reload
# signal), with an LTTng session capturing the CUDA domain.  Verifies:
#   - no crash across the disable -> RE-ENABLE -> disable cycle,
#   - cudaKernelActivity count == cudaKernelLaunch_begin count (every TRACING-window
#     kernel got its GPU activity record, including after MONITORING->TRACING
#     re-enable; nothing captured/leaked during MONITORING).
#
# CUPTI asymmetry vs ROCTracer (design doc §6.9.2): "off" cannot deregister the
# buffer callbacks, so it is cuptiActivityFlushAll + cuptiActivityDisable(all kinds).
# The blocking FlushAll before clearing cuda_activity_emit is what makes the
# equal-counts assertion hold — a batch collected under TRACING is never stranded
# for a later MONITORING delivery to drop.  Run with PINSIGHT_DEBUG_ACTIVITY=1 to
# see each transition's CUPTI return codes and any post-transition buffer drops.
#
# GOTCHA (same as HIP): the app is launched DIRECTLY (not via timeout/stdbuf) so $!
# is its real PID — a wrapper would catch SIGUSR1 itself (no handler -> exit 138).
#
# Usage: ./cyclic_mode_test.sh   (env: PINSIGHT_LIB, CUPTI_LIB_DIR, LTTNG_HOME)
set -u
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
PINSIGHT_LIB=${PINSIGHT_LIB:-$ROOT/build_cuda/libpinsight.so}
CUPTI_LIB_DIR=${CUPTI_LIB_DIR:-/usr/lib/x86_64-linux-gnu}
BIN=$(cd "$(dirname "$0")" && pwd)/looping_pinsight
OUT=${OUT:-$(dirname "$0")/cyclic_traces}
CFG=$(mktemp /tmp/cyc_cuda_cfg.XXXX.txt)
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
# device_activity defaults to off — the cyclic test needs it on to see activity records.
mkcfg() { printf '[CUDA.default]\n    trace_mode = %s\n    device_activity = on\n' "$1" > "$CFG"; }

mkcfg TRACING
lttng-sessiond --daemonize >/dev/null 2>&1; sleep 0.3
lttng destroy -a >/dev/null 2>&1
rm -rf "$OUT"
lttng create cyclic-cuda --output="$OUT" >/dev/null
lttng enable-event -u 'cupti_pinsight_lttng_ust:*' >/dev/null
lttng start >/dev/null

# 90 iters x 120 ms ~ 11 s ; switch every 3 s.  Run DIRECTLY (see GOTCHA).
env LD_PRELOAD="$PINSIGHT_LIB" PINSIGHT_TRACE_CONFIG_FILE="$CFG" "$BIN" 90 120 \
    >/dev/null 2>"$OUT".applog &
APP=$!
echo "looping_pinsight pid=$APP, cycling CUDA mode every 3s:"
sleep 3; mkcfg MONITORING; kill -USR1 $APP; echo "  -> MONITORING"
sleep 3; mkcfg TRACING;    kill -USR1 $APP; echo "  -> TRACING (activity re-enable)"
sleep 3; mkcfg MONITORING; kill -USR1 $APP; echo "  -> MONITORING"
wait $APP; rc=$?
lttng stop >/dev/null; lttng destroy >/dev/null 2>&1
rm -f "$CFG"

echo "app exit=$rc (0 = no crash)"
la=$("$BT" "$OUT" 2>/dev/null | grep -c cudaKernelLaunch_begin)
ac=$("$BT" "$OUT" 2>/dev/null | grep -c cudaKernelActivity)
echo "cudaKernelLaunch_begin=$la   cudaKernelActivity=$ac"
if [ "$rc" = 0 ] && [ "$la" -gt 0 ] && [ "$la" = "$ac" ]; then
    echo "PASS: cyclic mode + activity re-enable correct (counts match, no crash)"
else
    echo "FAIL: rc=$rc launches=$la activity=$ac (expected equal, non-zero, rc=0)"; exit 1
fi
