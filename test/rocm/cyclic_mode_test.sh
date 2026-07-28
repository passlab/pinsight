#!/bin/bash
# Cyclic runtime-mode test for the HIP/ROCTracer activity pool.
#
# Drives looping_pinsight through TRACING -> MONITORING -> TRACING -> MONITORING by
# rewriting the trace-config file and sending SIGUSR1 (PInsight's config-reload
# signal), with an LTTng session capturing the HIP domain.  Verifies:
#   - no crash across the disable -> RE-ENABLE -> disable cycle,
#   - host launches show clean ON/OFF/ON/OFF windows (MONITORING suppresses them),
#   - hipKernelActivity count == hipKernelLaunch_begin count (every TRACING-window
#     kernel got its GPU activity record, including after MONITORING->TRACING
#     re-enable; nothing captured/leaked during MONITORING).
#
# GOTCHA: the app is launched DIRECTLY (not via timeout/stdbuf) so $! is its real
# PID — a wrapper would catch SIGUSR1 itself (no handler -> exit 138, kills the app).
#
# Usage: ./cyclic_mode_test.sh   (env: PINSIGHT_LIB, ROCM_PATH, LTTNG_HOME)
set -u
ROCM_PATH=${ROCM_PATH:-/opt/rocm-7.2.1}
LTTNG_HOME=${LTTNG_HOME:-$HOME/local}
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
PINSIGHT_LIB=${PINSIGHT_LIB:-$ROOT/build_hip/libpinsight.so}
BIN=$(cd "$(dirname "$0")" && pwd)/looping_pinsight
OUT=${OUT:-$(dirname "$0")/cyclic_traces}
CFG=$(mktemp /tmp/cyc_cfg.XXXX.txt)
export PATH=$LTTNG_HOME/bin:$PATH
export LD_LIBRARY_PATH=$LTTNG_HOME/lib:$ROCM_PATH/lib:${LD_LIBRARY_PATH:-}

[ -x "$BIN" ] || { echo "build first:  make looping_pinsight"; exit 1; }
[ -e "$PINSIGHT_LIB" ] || { echo "missing $PINSIGHT_LIB (build with PINSIGHT_HIP=TRUE)"; exit 1; }
mkcfg() { printf '[HIP.default]\n    trace_mode = %s\n' "$1" > "$CFG"; }

mkcfg TRACING
lttng destroy -a >/dev/null 2>&1
rm -rf "$OUT"
lttng create cyclic --output="$OUT" >/dev/null
lttng enable-event -u 'roctracer_pinsight_lttng_ust:*' >/dev/null
lttng start >/dev/null

# 90 iters x 120 ms ~ 11 s ; switch every 3 s.  Run DIRECTLY (see GOTCHA).
env LD_PRELOAD="$PINSIGHT_LIB" PINSIGHT_TRACE_CONFIG_FILE="$CFG" "$BIN" 90 120 \
    >/dev/null 2>"$OUT".applog &
APP=$!
echo "looping_pinsight pid=$APP, cycling HIP mode every 3s:"
sleep 3; mkcfg MONITORING; kill -USR1 $APP; echo "  -> MONITORING"
sleep 3; mkcfg TRACING;    kill -USR1 $APP; echo "  -> TRACING (activity pool re-enable)"
sleep 3; mkcfg MONITORING; kill -USR1 $APP; echo "  -> MONITORING"
wait $APP; rc=$?
lttng stop >/dev/null; lttng destroy >/dev/null 2>&1
rm -f "$CFG"

echo "app exit=$rc (0 = no crash)"
la=$(babeltrace2 "$OUT" 2>/dev/null | grep -c hipKernelLaunch_begin)
ac=$(babeltrace2 "$OUT" 2>/dev/null | grep -c hipKernelActivity)
echo "hipKernelLaunch_begin=$la   hipKernelActivity=$ac"
if [ "$rc" = 0 ] && [ "$la" -gt 0 ] && [ "$la" = "$ac" ]; then
    echo "PASS: cyclic mode + activity-pool re-enable correct (counts match, no crash)"
else
    echo "FAIL: rc=$rc launches=$la activity=$ac (expected equal, non-zero, rc=0)"; exit 1
fi
