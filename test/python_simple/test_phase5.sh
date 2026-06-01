#!/bin/bash
# Phase 5 — Thread/Punit Filtering for Python tracing.
#
# Verifies that [Lexgion(Python:funcname)] : Python.default : Python.thread(N)
# config entries restrict tracing to the specified thread IDs, that non-matching
# threads are suppressed, and that function_begin/end counts are paired.
#
# Thread-ID semantics (pysysmon_get_thread_id):
#   Thread 0 = main Python thread (assigned before worker threads start)
#   Threads 1-NWORKERS = worker threads (assigned at first on_py_start)
#
# The Python domain starts in STANDBY mode inside libpinsight.so so that stdlib
# imports do not consume per-thread lexgion cache slots.  The launcher calls
# _pinsight_python.begin_tracing() after activate() to switch to TRACING
# before running the user script.  No special two-config workaround is needed.
#
# Usage: ./test_phase5.sh [PINSIGHT_BUILD_DIR]
#   PINSIGHT_BUILD_DIR defaults to ../../build

set -euo pipefail

BUILD=${1:-../../build}
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PINSIGHT="$BUILD/libpinsight.so"
PYTHONPATH_VAL="$BUILD"
N=30          # iterations per function per thread
NWORKERS=4    # worker threads (IDs 1-4)
PASS=0
FAIL=0

run_cmd() {
    LD_PRELOAD="$PINSIGHT" PYTHONPATH="$PYTHONPATH_VAL" \
        python3 -m pinsight "$@" 2>/dev/null
}

# ── LTTng session helpers ──────────────────────────────────────────────────
SESSION="pinsight-phase5"

session_start() {
    lttng destroy "$SESSION" 2>/dev/null || true
    lttng create "$SESSION" --output="/tmp/pinsight-phase5-trace" >/dev/null
    lttng enable-event --userspace "pysysmon_pinsight_lttng_ust:*" >/dev/null
    lttng start >/dev/null
}

count_begin() {
    local qualname="$1"
    lttng view 2>/dev/null \
        | grep "function_begin" \
        | grep -c "qualname = \"${qualname}\"" || true
}

count_end() {
    local qualname="$1"
    lttng view 2>/dev/null \
        | grep "function_end" \
        | grep -c "qualname = \"${qualname}\"" || true
}

# Count function_begin events for qualname on a specific python_thread_id
count_begin_thread() {
    local qualname="$1"
    local tid="$2"
    lttng view 2>/dev/null \
        | grep "function_begin" \
        | grep "qualname = \"${qualname}\"" \
        | grep -c "python_thread_id = ${tid}" || true
}

session_stop() {
    lttng stop >/dev/null 2>&1
}

session_destroy() {
    lttng destroy "$SESSION" >/dev/null 2>&1 || true
    rm -rf /tmp/pinsight-phase5-trace
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
      range)
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
echo "===== Phase 5.1: Thread filter — only threads 1-2 trace worker ====="
echo ""
# ═══════════════════════════════════════════════════════════════════════════
# 4 worker threads (IDs 1-4).  Filter to threads 1 and 2, max=8 each.
# Expected worker traces: 8 (thread 1) + 8 (thread 2) = 16.
# Threads 3 and 4 are excluded by the punit filter.
# main_work (thread 0) has its own named config with thread 0 filter (max=5).

cat > /tmp/pinsight_trace_config.txt <<'EOF'
[Lexgion(Python).default]
    max_num_traces = 0

[Lexgion(Python:worker)] : Python.default : Python.thread(1-2)
    max_num_traces = 8

[Lexgion(Python:main_work)] : Python.default : Python.thread(0)
    max_num_traces = 5
EOF

session_start
PINSIGHT_TRACE_CONFIG_FILE=/tmp/pinsight_trace_config.txt \
    run_cmd "$SCRIPT_DIR/test_thread_filter.py" $N $NWORKERS
session_stop
WORKER=$(count_begin "worker")
MAINWORK=$(count_begin "main_work")
THREAD1=$(count_begin_thread "worker" 1)
THREAD2=$(count_begin_thread "worker" 2)
THREAD3=$(count_begin_thread "worker" 3)
THREAD4=$(count_begin_thread "worker" 4)
session_destroy

check "5.1a worker total traces (threads 1-2, max=8 each → 16)" "$WORKER" 16 eq
check "5.1b thread 1 traced (max=8)" "$THREAD1" 8 eq
check "5.1c thread 2 traced (max=8)" "$THREAD2" 8 eq
check "5.1d thread 3 suppressed (punit filter)" "$THREAD3" 0 eq
check "5.1e thread 4 suppressed (punit filter)" "$THREAD4" 0 eq
check "5.1f main_work traced on thread 0 (max=5)" "$MAINWORK" 5 eq

# ═══════════════════════════════════════════════════════════════════════════
echo ""
echo "===== Phase 5.2: Thread filter — threads 3-4 only ====="
echo ""
# ═══════════════════════════════════════════════════════════════════════════
# Filter worker to threads 3-4.  max=6 per thread → 12 total.
# Threads 1-2 are excluded.

cat > /tmp/pinsight_trace_config.txt <<'EOF'
[Lexgion(Python).default]
    max_num_traces = 0

[Lexgion(Python:worker)] : Python.default : Python.thread(3-4)
    max_num_traces = 6
EOF

session_start
PINSIGHT_TRACE_CONFIG_FILE=/tmp/pinsight_trace_config.txt \
    run_cmd "$SCRIPT_DIR/test_thread_filter.py" $N $NWORKERS
session_stop
WORKER=$(count_begin "worker")
THREAD1=$(count_begin_thread "worker" 1)
THREAD2=$(count_begin_thread "worker" 2)
THREAD3=$(count_begin_thread "worker" 3)
THREAD4=$(count_begin_thread "worker" 4)
session_destroy

check "5.2a worker total traces (threads 3-4, max=6 each → 12)" "$WORKER" 12 eq
check "5.2b thread 1 suppressed" "$THREAD1" 0 eq
check "5.2c thread 2 suppressed" "$THREAD2" 0 eq
check "5.2d thread 3 traced (max=6)" "$THREAD3" 6 eq
check "5.2e thread 4 traced (max=6)" "$THREAD4" 6 eq

# ═══════════════════════════════════════════════════════════════════════════
echo ""
echo "===== Phase 5.3: function_begin / function_end pairing with thread filter ====="
echo ""
# ═══════════════════════════════════════════════════════════════════════════
# Threads 1-2 trace worker with max=9.  Verify begin==end (paired).

cat > /tmp/pinsight_trace_config.txt <<'EOF'
[Lexgion(Python).default]
    max_num_traces = 0

[Lexgion(Python:worker)] : Python.default : Python.thread(1-2)
    max_num_traces = 9
EOF

session_start
PINSIGHT_TRACE_CONFIG_FILE=/tmp/pinsight_trace_config.txt \
    run_cmd "$SCRIPT_DIR/test_thread_filter.py" $N $NWORKERS
session_stop
BEGIN=$(count_begin "worker")
END=$(count_end "worker")
session_destroy

check "5.3a worker function_begin count (9*2=18)" "$BEGIN" 18 eq
check "5.3b worker function_end count matches begin (pairing)" "$END" "$BEGIN" eq

# ═══════════════════════════════════════════════════════════════════════════
echo ""
echo "===== Phase 5.4: Domain default fallback alongside thread filter ====="
echo ""
# ═══════════════════════════════════════════════════════════════════════════
# worker on threads 1-2: named + thread filter (max=7 each → 14 total).
# main_work on thread 0: named config, thread filter to thread 0 (max=5).
# Lexgion.default max=0 → worker on threads 3-4 suppressed.

cat > /tmp/pinsight_trace_config.txt <<'EOF'
[Lexgion(Python).default]
    max_num_traces = 0

[Lexgion(Python:worker)] : Python.default : Python.thread(1-2)
    max_num_traces = 7

[Lexgion(Python:main_work)] : Python.default : Python.thread(0)
    max_num_traces = 5
EOF

session_start
PINSIGHT_TRACE_CONFIG_FILE=/tmp/pinsight_trace_config.txt \
    run_cmd "$SCRIPT_DIR/test_thread_filter.py" $N $NWORKERS
session_stop
WORKER=$(count_begin "worker")
MAINWORK=$(count_begin "main_work")
session_destroy

check "5.4a worker named+thread filter (max=7 each, threads 1-2 → total 14)" "$WORKER" 14 eq
check "5.4b main_work thread-filtered to thread 0 (max=5)" "$MAINWORK" 5 eq

# ═══════════════════════════════════════════════════════════════════════════
echo ""
echo "===== Phase 5.5: All workers traced when thread range covers all ====="
echo ""
# ═══════════════════════════════════════════════════════════════════════════
# Thread filter Python.thread(1-4) covers all 4 worker threads.
# max=5 → each worker thread traces 5 → total 20.

cat > /tmp/pinsight_trace_config.txt <<'EOF'
[Lexgion(Python).default]
    max_num_traces = 0

[Lexgion(Python:worker)] : Python.default : Python.thread(1-4)
    max_num_traces = 5
EOF

session_start
PINSIGHT_TRACE_CONFIG_FILE=/tmp/pinsight_trace_config.txt \
    run_cmd "$SCRIPT_DIR/test_thread_filter.py" $N $NWORKERS
session_stop
WORKER=$(count_begin "worker")
session_destroy

check "5.5 worker all 4 threads (1-4), max=5 each → total 20" "$WORKER" 20 eq

# ── Cleanup ──────────────────────────────────────────────────────────────
rm -f /tmp/pinsight_trace_config.txt

echo ""
echo "=========================================="
echo "Phase 5 Results: $PASS passed, $FAIL failed"
echo "=========================================="
[[ $FAIL -eq 0 ]]
