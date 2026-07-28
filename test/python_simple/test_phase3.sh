#!/bin/bash
# Phase 3 — Rate Control and Trace-Mode-After tests for Python tracing.
#
# Usage: ./test_phase3.sh [PINSIGHT_BUILD_DIR]
#   PINSIGHT_BUILD_DIR defaults to ../../build

set -euo pipefail

BUILD=${1:-../../build}
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PINSIGHT="$BUILD/libpinsight.so"
PYTHONPATH_VAL="$BUILD"
N=100          # iterations per function (> max_num_traces used in configs)
PASS=0
FAIL=0

run_cmd() {
    LD_PRELOAD="$PINSIGHT" PYTHONPATH="$PYTHONPATH_VAL" \
        python3 -m pinsight "$@" 2>/dev/null
}

# ── LTTng session helpers ──────────────────────────────────────────────────
SESSION="pinsight-phase3"

session_start() {
    lttng destroy "$SESSION" 2>/dev/null || true
    lttng create "$SESSION" --output="/tmp/pinsight-phase3-trace" >/dev/null
    lttng enable-event --userspace "pysysmon_pinsight_lttng_ust:*" >/dev/null
    lttng start >/dev/null
}

session_stop_and_count() {
    # $1 = qualname to count (function_begin events only)
    lttng stop >/dev/null 2>&1
    local qualname="${1:-}"
    local count
    if [[ -n "$qualname" ]]; then
        count=$(lttng view 2>/dev/null \
            | grep "function_begin" \
            | grep -c "qualname = \"${qualname}\"" || true)
    else
        count=$(lttng view 2>/dev/null | grep -c "function_begin" || true)
    fi
    lttng destroy "$SESSION" >/dev/null 2>&1 || true
    rm -rf /tmp/pinsight-phase3-trace
    echo "$count"
}

check() {
    local label="$1" result="$2" expected="$3" op="${4:-eq}"
    case "$op" in
      eq)
        if [[ "$result" -eq "$expected" ]]; then
            echo "[PASS] $label: got $result (expected $expected)"
            PASS=$(( PASS + 1 ))
        else
            echo "[FAIL] $label: got $result (expected $expected)"
            FAIL=$(( FAIL + 1 ))
        fi
        ;;
      ge)
        if [[ "$result" -ge "$expected" ]]; then
            echo "[PASS] $label: got $result (expected >=$expected)"
            PASS=$(( PASS + 1 ))
        else
            echo "[FAIL] $label: got $result (expected >=$expected, got $result)"
            FAIL=$(( FAIL + 1 ))
        fi
        ;;
      range)
        # expected = "lo:hi"
        local lo="${expected%%:*}" hi="${expected##*:}"
        if [[ "$result" -ge "$lo" && "$result" -le "$hi" ]]; then
            echo "[PASS] $label: got $result (expected ${lo}-${hi})"
            PASS=$(( PASS + 1 ))
        else
            echo "[FAIL] $label: got $result (expected ${lo}-${hi})"
            FAIL=$(( FAIL + 1 ))
        fi
        ;;
    esac
}

cd "$SCRIPT_DIR"

# ═══════════════════════════════════════════════════════════════════════════
echo ""
echo "===== Phase 3.1: max_num_traces =====";
echo ""
# ═══════════════════════════════════════════════════════════════════════════
# hot_loop called $N=100 times. max_num_traces=10 → expect exactly 10 traces.

cat > /tmp/pinsight_trace_config.txt <<'EOF'
[Lexgion(Python:hot_loop)]
    max_num_traces = 10

[Lexgion(Python:sampled_work)]
    max_num_traces = 0

[Lexgion(Python:once_then_stop)]
    max_num_traces = 0
EOF

session_start
PINSIGHT_TRACE_CONFIG_FILE=/tmp/pinsight_trace_config.txt \
    run_cmd "$SCRIPT_DIR/test_rate_control.py" $N
COUNT=$(session_stop_and_count "hot_loop")
check "3.1a max_num_traces=10 for hot_loop ($N calls)" "$COUNT" 10 eq

# ── also verify the suppressed functions emitted 0 events ──
session_start
PINSIGHT_TRACE_CONFIG_FILE=/tmp/pinsight_trace_config.txt \
    run_cmd "$SCRIPT_DIR/test_rate_control.py" $N
SUPPRESSED=$(lttng stop >/dev/null 2>&1; \
    lttng view 2>/dev/null | grep "function_begin" | \
    grep -E '"sampled_work"|"once_then_stop"' | wc -l || true)
lttng destroy "$SESSION" >/dev/null 2>&1 || true
rm -rf /tmp/pinsight-phase3-trace
check "3.1b max_num_traces=0 suppresses sampled_work and once_then_stop" "$SUPPRESSED" 0 eq

# ── trace_starts_at: skip first 5, then trace up to 10 ──
cat > /tmp/pinsight_trace_config.txt <<'EOF'
[Lexgion(Python:hot_loop)]
    trace_starts_at = 5
    max_num_traces  = 10
EOF

session_start
PINSIGHT_TRACE_CONFIG_FILE=/tmp/pinsight_trace_config.txt \
    run_cmd "$SCRIPT_DIR/test_rate_control.py" $N
COUNT=$(session_stop_and_count "hot_loop")
check "3.1c trace_starts_at=5 + max_num_traces=10 → 10 traces" "$COUNT" 10 eq

# ═══════════════════════════════════════════════════════════════════════════
echo ""
echo "===== Phase 3.2: tracing_rate =====";
echo ""
# ═══════════════════════════════════════════════════════════════════════════
# sampled_work called N=100 times. tracing_rate=5 → expect ~N/rate = 20 traces.
# Allow ±2 tolerance for off-by-one in the rate counter.

cat > /tmp/pinsight_trace_config.txt <<'EOF'
[Lexgion(Python:sampled_work)]
    tracing_rate = 5
EOF

session_start
PINSIGHT_TRACE_CONFIG_FILE=/tmp/pinsight_trace_config.txt \
    run_cmd "$SCRIPT_DIR/test_rate_control.py" $N
COUNT=$(session_stop_and_count "sampled_work")
check "3.2a tracing_rate=5, $N calls → ~20 traces" "$COUNT" "18:22" range

# ── tracing_rate=10 → ~10 traces ──
cat > /tmp/pinsight_trace_config.txt <<'EOF'
[Lexgion(Python:sampled_work)]
    tracing_rate = 10
EOF

session_start
PINSIGHT_TRACE_CONFIG_FILE=/tmp/pinsight_trace_config.txt \
    run_cmd "$SCRIPT_DIR/test_rate_control.py" $N
COUNT=$(session_stop_and_count "sampled_work")
check "3.2b tracing_rate=10, $N calls → ~10 traces" "$COUNT" "8:12" range

# ── rate=1 (every call) → exactly N traces ──
cat > /tmp/pinsight_trace_config.txt <<'EOF'
[Lexgion(Python:hot_loop)]
    tracing_rate = 1
    max_num_traces = -1
EOF

session_start
PINSIGHT_TRACE_CONFIG_FILE=/tmp/pinsight_trace_config.txt \
    run_cmd "$SCRIPT_DIR/test_rate_control.py" $N
COUNT=$(session_stop_and_count "hot_loop")
check "3.2c tracing_rate=1 (every call), $N calls → $N traces" "$COUNT" $N eq

# ═══════════════════════════════════════════════════════════════════════════
echo ""
echo "===== Phase 3.3: window_end_action =====";
echo ""
# ═══════════════════════════════════════════════════════════════════════════

# ── 3.3a: once_then_stop traces 20, then domain drops to MONITORING ──
# After the trigger, hot_loop (run second) should get 0 traces.
# Use a dedicated script that runs once_then_stop first, then hot_loop.
cat > /tmp/pinsight_trace_config.txt <<'EOF'
[Python.default]
    trace_mode = TRACING

[Lexgion(Python:once_then_stop)]
    max_num_traces  = 20
    window_end_action = Python:MONITORING
EOF

cat > /tmp/test_3_3ab.py <<PYEOF
import sys
N = int(sys.argv[1]) if len(sys.argv) > 1 else 100

def once_then_stop(i):
    pass

def hot_loop(i):
    pass

for i in range(N):
    once_then_stop(i)

for i in range(N):
    hot_loop(i)
PYEOF

session_start
PINSIGHT_TRACE_CONFIG_FILE=/tmp/pinsight_trace_config.txt \
    run_cmd /tmp/test_3_3ab.py $N
lttng stop >/dev/null 2>&1
# once_then_stop should have exactly 20 traces
STOP_COUNT=$(lttng view 2>/dev/null | grep "function_begin" \
    | grep -c '"once_then_stop"' || true)
# hot_loop runs after trigger → domain is MONITORING → 0 traces
HOT_COUNT=$(lttng view 2>/dev/null | grep "function_begin" \
    | grep -c '"hot_loop"' || true)
lttng destroy "$SESSION" >/dev/null 2>&1 || true
rm -rf /tmp/pinsight-phase3-trace
check "3.3a once_then_stop emits 20 traces before trigger" "$STOP_COUNT" 20 eq
check "3.3b hot_loop emits 0 traces (domain already MONITORING by the time it runs)" "$HOT_COUNT" 0 eq

# ── 3.3c: window_end_action = Python:STANDBY ──
# Verify that after the trigger, the domain is in STANDBY (still counting but not tracing)
cat > /tmp/pinsight_trace_config.txt <<'EOF'
[Python.default]
    trace_mode = TRACING

[Lexgion(Python:once_then_stop)]
    max_num_traces  = 15
    window_end_action = Python:STANDBY
EOF

session_start
PINSIGHT_TRACE_CONFIG_FILE=/tmp/pinsight_trace_config.txt \
    run_cmd "$SCRIPT_DIR/test_rate_control.py" $N
STOP_COUNT=$(session_stop_and_count "once_then_stop")
check "3.3c window_end_action=STANDBY: exactly 15 traces then silence" "$STOP_COUNT" 15 eq

# ── 3.3d: Lexgion(Python).default max_num_traces ──
# All Python functions share max_num_traces=5 via domain default.
cat > /tmp/pinsight_trace_config.txt <<'EOF'
[Lexgion(Python).default]
    max_num_traces = 5
EOF

session_start
PINSIGHT_TRACE_CONFIG_FILE=/tmp/pinsight_trace_config.txt \
    run_cmd "$SCRIPT_DIR/test_rate_control.py" $N
lttng stop >/dev/null 2>&1
HOT=$(lttng view 2>/dev/null | grep "function_begin" | grep -c '"hot_loop"' || true)
SAM=$(lttng view 2>/dev/null | grep "function_begin" | grep -c '"sampled_work"' || true)
ONCE=$(lttng view 2>/dev/null | grep "function_begin" | grep -c '"once_then_stop"' || true)
lttng destroy "$SESSION" >/dev/null 2>&1 || true
rm -rf /tmp/pinsight-phase3-trace
check "3.3d Lexgion(Python).default max=5: hot_loop" "$HOT" 5 eq
check "3.3d Lexgion(Python).default max=5: sampled_work" "$SAM" 5 eq
check "3.3d Lexgion(Python).default max=5: once_then_stop" "$ONCE" 5 eq

# ═══════════════════════════════════════════════════════════════════════════
echo ""
echo "===== Phase 3.4: INTROSPECT =====";
echo ""
# ═══════════════════════════════════════════════════════════════════════════
# Write a simple introspect script that records it was called.
INTROSPECT_FLAG=/tmp/pinsight_introspect_called
rm -f "$INTROSPECT_FLAG"
cat > /tmp/introspect_script.sh <<EOF
#!/bin/bash
touch $INTROSPECT_FLAG
EOF
chmod +x /tmp/introspect_script.sh

cat > /tmp/pinsight_trace_config.txt <<EOF
[Python.default]
    trace_mode = TRACING

[Lexgion(Python:hot_loop)]
    max_num_traces   = 10
    window_end_action = INTROSPECT:5:/tmp/introspect_script.sh:Python:TRACING
EOF

session_start
PINSIGHT_TRACE_CONFIG_FILE=/tmp/pinsight_trace_config.txt \
    run_cmd "$SCRIPT_DIR/test_rate_control.py" $N
HOT=$(session_stop_and_count "hot_loop")

if [[ -f "$INTROSPECT_FLAG" ]]; then
    echo "[PASS] 3.4a INTROSPECT: script /tmp/introspect_script.sh was executed"
    PASS=$(( PASS + 1 ))
else
    echo "[FAIL] 3.4a INTROSPECT: script was NOT executed (flag file missing)"
    FAIL=$(( FAIL + 1 ))
fi
# hot_loop should have exactly 10 traces (hits max before trigger, fires once)
check "3.4b INTROSPECT: hot_loop emitted 10 traces before pause" "$HOT" 10 eq
rm -f "$INTROSPECT_FLAG" /tmp/introspect_script.sh

# ═══════════════════════════════════════════════════════════════════════════
echo ""
echo "===== Phase 3.5: SIGUSR1 config reload =====";
echo ""
# ═══════════════════════════════════════════════════════════════════════════
# Start with max_num_traces=5, then reload with max_num_traces=10.
# The function is called in two batches of N; second batch picks up new config.

cat > /tmp/pinsight_trace_config_v1.txt <<'EOF'
[Lexgion(Python:hot_loop)]
    max_num_traces = 5
EOF

cat > /tmp/pinsight_trace_config_v2.txt <<'EOF'
[Lexgion(Python:hot_loop)]
    max_num_traces = 10
EOF

# Write a test script that:
#  1. calls hot_loop N times (→ 5 traces with v1 config)
#  2. sends SIGUSR1 to itself and copies v2 over v1
#  3. calls hot_loop N more times (→ up to 10 more traces from fresh counter)
cat > /tmp/test_sigusr1.py <<'EOF'
import os, sys, signal, time

def hot_loop(i):
    pass

N = 50
for i in range(N):
    hot_loop(i)

# Copy v2 config over v1 and send SIGUSR1 to trigger reload
import shutil
shutil.copy("/tmp/pinsight_trace_config_v2.txt",
            "/tmp/pinsight_trace_config_v1.txt")
os.kill(os.getpid(), signal.SIGUSR1)
time.sleep(0.2)   # let control thread process the reload

for i in range(N):
    hot_loop(i)
EOF

session_start
PINSIGHT_TRACE_CONFIG_FILE=/tmp/pinsight_trace_config_v1.txt \
    run_cmd /tmp/test_sigusr1.py
HOT=$(session_stop_and_count "hot_loop")
# First batch: 5 traces. After reload, trace_counter resets → up to 10 more.
# Total should be between 10 and 15 (5 before + up to 10 after).
check "3.5 SIGUSR1 reload: hot_loop traces after reload (expect 10-15)" "$HOT" "10:15" range

# ── Cleanup ──────────────────────────────────────────────────────────────
rm -f /tmp/pinsight_trace_config.txt \
      /tmp/pinsight_trace_config_v1.txt \
      /tmp/pinsight_trace_config_v2.txt \
      /tmp/test_sigusr1.py

echo ""
echo "=========================================="
echo "Phase 3 Results: $PASS passed, $FAIL failed"
echo "=========================================="
[[ $FAIL -eq 0 ]]
