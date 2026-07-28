#!/bin/bash
# window_timeout test for the per-process wall-clock window deadline.
#
# Runs looping_pinsight in TRACING with a [Lexgion.default] window_timeout that is
# much shorter than the run, and window_end_action = HIP:MONITORING. The control
# thread's sem_timedwait must fire at the deadline and flip HIP -> MONITORING even
# though NO region reaches max_num_traces (the standalone time-windowed capture).
# Verifies:
#   - no crash (app exits 0),
#   - the timer actually fired ("window_timeout (Ns) reached" in the app log),
#   - tracing ENDED EARLY: host kernel launches < total iterations (the window
#     closed before the run finished) yet > 0 (some TRACING window was captured),
#   - hipKernelActivity == hipKernelLaunch_begin (every traced kernel got its GPU
#     activity record; nothing captured/leaked after the switch to MONITORING).
#
# GOTCHA: launch the app DIRECTLY so $! is its real PID (a wrapper would change
# timing/signals) — same discipline as cyclic_mode_test.sh.
#
# Usage: ./window_timeout_test.sh   (env: PINSIGHT_LIB, ROCM_PATH, LTTNG_HOME)
set -u
ROCM_PATH=${ROCM_PATH:-/opt/rocm-7.2.1}
LTTNG_HOME=${LTTNG_HOME:-$HOME/local}
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
PINSIGHT_LIB=${PINSIGHT_LIB:-$ROOT/build_hip/libpinsight.so}
BIN=$(cd "$(dirname "$0")" && pwd)/looping_pinsight
OUT=${OUT:-$(dirname "$0")/window_traces}
CFG=$(mktemp /tmp/win_cfg.XXXX.txt)
export PATH=$LTTNG_HOME/bin:$PATH
export LD_LIBRARY_PATH=$LTTNG_HOME/lib:$ROCM_PATH/lib:${LD_LIBRARY_PATH:-}

ITERS=${ITERS:-100}        # 100 x 100ms ~ 10s run
MS=${MS:-100}
WINDOW=${WINDOW:-3}        # end the TRACING window after 3 wall seconds

[ -x "$BIN" ] || { echo "build first:  make looping_pinsight"; exit 1; }
[ -e "$PINSIGHT_LIB" ] || { echo "missing $PINSIGHT_LIB (build with PINSIGHT_HIP=TRUE)"; exit 1; }

# TRACING now, but bounded: end the window after $WINDOW s, then HIP -> MONITORING.
cat > "$CFG" <<EOF
[HIP.default]
    trace_mode = TRACING
[Lexgion.default]
    window_timeout = $WINDOW
    window_end_action = HIP:MONITORING
EOF

lttng destroy -a >/dev/null 2>&1
rm -rf "$OUT"
lttng create window --output="$OUT" >/dev/null
lttng enable-event -u 'roctracer_pinsight_lttng_ust:*' >/dev/null
lttng start >/dev/null

echo "Running $BIN $ITERS $MS with window_timeout=${WINDOW}s -> HIP:MONITORING"
env LD_PRELOAD="$PINSIGHT_LIB" PINSIGHT_TRACE_CONFIG_FILE="$CFG" "$BIN" "$ITERS" "$MS" \
    >/dev/null 2>"$OUT".applog &
APP=$!
wait $APP; rc=$?
lttng stop >/dev/null; lttng destroy >/dev/null 2>&1
rm -f "$CFG"

fired=$(grep -c "window_timeout (${WINDOW}s) reached" "$OUT".applog)
la=$(babeltrace2 "$OUT" 2>/dev/null | grep -c hipKernelLaunch_begin)
ac=$(babeltrace2 "$OUT" 2>/dev/null | grep -c hipKernelActivity)
echo "app exit=$rc (0 = no crash)"
echo "timer fired=$fired   hipKernelLaunch_begin=$la   hipKernelActivity=$ac   (iters=$ITERS)"

if [ "$rc" = 0 ] && [ "$fired" -ge 1 ] && [ "$la" -gt 0 ] && [ "$la" -lt "$ITERS" ] \
   && [ "$la" = "$ac" ]; then
    echo "PASS: window ended on deadline (early), activity capture stopped cleanly, no crash"
else
    echo "FAIL: rc=$rc fired=$fired launches=$la activity=$ac iters=$ITERS"
    echo "      (expected rc=0, fired>=1, 0<launches<iters, launches==activity)"; exit 1
fi
