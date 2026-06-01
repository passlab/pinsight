#!/bin/bash
# Phase 4 — Named Lexgion Runtime Matching for Python tracing.
#
# Verifies that [Lexgion(Python:name)] config entries are resolved at runtime
# by co_qualname, that filename hints disambiguate same-named functions across
# files, that class method dotted names work, that unlisted functions fall
# through to domain default, and that SIGUSR1 reload re-matches correctly.
#
# Usage: ./test_phase4.sh [PINSIGHT_BUILD_DIR]
#   PINSIGHT_BUILD_DIR defaults to ../../build

set -euo pipefail

BUILD=${1:-../../build}
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PINSIGHT="$BUILD/libpinsight.so"
PYTHONPATH_VAL="$BUILD"
N=50          # iterations per function
PASS=0
FAIL=0

run_cmd() {
    LD_PRELOAD="$PINSIGHT" PYTHONPATH="$PYTHONPATH_VAL" \
        python3 -m pinsight "$@" 2>/dev/null
}

# ── LTTng session helpers ──────────────────────────────────────────────────
SESSION="pinsight-phase4"

session_start() {
    lttng destroy "$SESSION" 2>/dev/null || true
    lttng create "$SESSION" --output="/tmp/pinsight-phase4-trace" >/dev/null
    lttng enable-event --userspace "pysysmon_pinsight_lttng_ust:*" >/dev/null
    lttng start >/dev/null
}

# Count function_begin events for a given qualname
count_begin() {
    local qualname="$1"
    lttng view 2>/dev/null \
        | grep "function_begin" \
        | grep -c "qualname = \"${qualname}\"" || true
}

# Count function_end events for a given qualname
count_end() {
    local qualname="$1"
    lttng view 2>/dev/null \
        | grep "function_end" \
        | grep -c "qualname = \"${qualname}\"" || true
}

session_stop() {
    lttng stop >/dev/null 2>&1
}

session_destroy() {
    lttng destroy "$SESSION" >/dev/null 2>&1 || true
    rm -rf /tmp/pinsight-phase4-trace
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
echo "===== Phase 4.1: Basic named match — only named function is traced ====="
echo ""
# ═══════════════════════════════════════════════════════════════════════════
# [Lexgion(Python:compute)] max=10. Domain default max=0 (no tracing).
# compute() → 10 traces; preprocess/postprocess → 0.

cat > /tmp/pinsight_trace_config.txt <<'EOF'
[Lexgion(Python).default]
    max_num_traces = 0

[Lexgion(Python:compute)]
    max_num_traces = 10
EOF

session_start
PINSIGHT_TRACE_CONFIG_FILE=/tmp/pinsight_trace_config.txt \
    run_cmd "$SCRIPT_DIR/test_named_lexgion.py" $N
session_stop
COMPUTE=$(count_begin "compute")
PREPROC=$(count_begin "preprocess")
POSTPROC=$(count_begin "postprocess")
session_destroy

check "4.1a compute traced (named, max=10)" "$COMPUTE" 10 eq
check "4.1b preprocess suppressed (domain default max=0)" "$PREPROC" 0 eq
check "4.1c postprocess suppressed (domain default max=0)" "$POSTPROC" 0 eq

# ═══════════════════════════════════════════════════════════════════════════
echo ""
echo "===== Phase 4.2: Class method dotted qualname ====="
echo ""
# ═══════════════════════════════════════════════════════════════════════════
# [Lexgion(Python:Solver.run)] max=8. Solver.setup falls to default (max=0).

cat > /tmp/pinsight_trace_config.txt <<'EOF'
[Lexgion(Python).default]
    max_num_traces = 0

[Lexgion(Python:Solver.run)]
    max_num_traces = 8
EOF

session_start
PINSIGHT_TRACE_CONFIG_FILE=/tmp/pinsight_trace_config.txt \
    run_cmd "$SCRIPT_DIR/test_named_lexgion.py" $N
session_stop
RUN=$(count_begin "Solver.run")
SETUP=$(count_begin "Solver.setup")
session_destroy

check "4.2a Solver.run traced (named, max=8)" "$RUN" 8 eq
check "4.2b Solver.setup suppressed (domain default max=0)" "$SETUP" 0 eq

# ═══════════════════════════════════════════════════════════════════════════
echo ""
echo "===== Phase 4.3: Domain default fallback with different limits ====="
echo ""
# ═══════════════════════════════════════════════════════════════════════════
# Named: compute max=12, Solver.run max=6.
# Domain default max=3 → preprocess, postprocess, Solver.setup each get 3.

cat > /tmp/pinsight_trace_config.txt <<'EOF'
[Lexgion(Python).default]
    max_num_traces = 3

[Lexgion(Python:compute)]
    max_num_traces = 12

[Lexgion(Python:Solver.run)]
    max_num_traces = 6
EOF

session_start
PINSIGHT_TRACE_CONFIG_FILE=/tmp/pinsight_trace_config.txt \
    run_cmd "$SCRIPT_DIR/test_named_lexgion.py" $N
session_stop
COMPUTE=$(count_begin "compute")
RUN=$(count_begin "Solver.run")
PREPROC=$(count_begin "preprocess")
POSTPROC=$(count_begin "postprocess")
SETUP=$(count_begin "Solver.setup")
session_destroy

check "4.3a compute named max=12" "$COMPUTE" 12 eq
check "4.3b Solver.run named max=6" "$RUN" 6 eq
check "4.3c preprocess domain default max=3" "$PREPROC" 3 eq
check "4.3d postprocess domain default max=3" "$POSTPROC" 3 eq
check "4.3e Solver.setup domain default max=3" "$SETUP" 3 eq

# ═══════════════════════════════════════════════════════════════════════════
echo ""
echo "===== Phase 4.4: Filename disambiguation ====="
echo ""
# ═══════════════════════════════════════════════════════════════════════════
# Both the inline script and test_named_lexgion_helper.py define 'compute'.
# Without a filename hint, [Lexgion(Python:compute)] matches BOTH.
# With hint [Lexgion(Python:test_named_lexgion_helper.py:compute)], only the
# helper's compute is traced; the inline compute falls to domain default (max=0).

# Create an inline script that defines its own compute AND calls helper.compute.
# The inline compute has co_filename=/tmp/test_4_4_main.py,
# helper.compute has co_filename=.../test_named_lexgion_helper.py.
cat > /tmp/test_4_4_main.py <<PYEOF
import sys, os
sys.path.insert(0, "$SCRIPT_DIR")
import test_named_lexgion_helper as helper

N = int(sys.argv[1]) if len(sys.argv) > 1 else 50

def compute(i):   # co_qualname='compute', co_filename='/tmp/test_4_4_main.py'
    pass

for i in range(N):
    compute(i)          # inline compute — should NOT be traced (no hint match)

for i in range(N):
    helper.compute(i)   # helper compute — SHOULD be traced (hint matches)
PYEOF

cat > /tmp/pinsight_trace_config.txt <<'EOF'
[Lexgion(Python).default]
    max_num_traces = 0

[Lexgion(Python:test_named_lexgion_helper.py:compute)]
    max_num_traces = 7
EOF

session_start
PINSIGHT_TRACE_CONFIG_FILE=/tmp/pinsight_trace_config.txt \
    run_cmd /tmp/test_4_4_main.py $N
session_stop
# Count by filename appearing in the event output
HELPER_COMPUTE=$(lttng view 2>/dev/null \
    | grep "function_begin" \
    | grep 'qualname = "compute"' \
    | grep -c "test_named_lexgion_helper" || true)
MAIN_COMPUTE=$(lttng view 2>/dev/null \
    | grep "function_begin" \
    | grep 'qualname = "compute"' \
    | grep -c "test_4_4_main" || true)
session_destroy

check "4.4a helper compute traced (filename hint, max=7)" "$HELPER_COMPUTE" 7 eq
check "4.4b inline compute suppressed (no filename hint match, default max=0)" "$MAIN_COMPUTE" 0 eq

# ═══════════════════════════════════════════════════════════════════════════
echo ""
echo "===== Phase 4.5: function_begin / function_end pairing ====="
echo ""
# ═══════════════════════════════════════════════════════════════════════════
# For a named function with max=9, both function_begin AND function_end
# must appear exactly 9 times — always paired.

cat > /tmp/pinsight_trace_config.txt <<'EOF'
[Lexgion(Python).default]
    max_num_traces = 0

[Lexgion(Python:compute)]
    max_num_traces = 9
EOF

session_start
PINSIGHT_TRACE_CONFIG_FILE=/tmp/pinsight_trace_config.txt \
    run_cmd "$SCRIPT_DIR/test_named_lexgion.py" $N
session_stop
BEGIN=$(count_begin "compute")
END=$(count_end "compute")
session_destroy

check "4.5a compute function_begin count" "$BEGIN" 9 eq
check "4.5b compute function_end count matches begin (pairing)" "$END" "$BEGIN" eq

# ═══════════════════════════════════════════════════════════════════════════
echo ""
echo "===== Phase 4.6: Domain-qualified name vs no-domain name ====="
echo ""
# ═══════════════════════════════════════════════════════════════════════════
# [Lexgion(Python:compute)] is domain-specific (Python domain only).
# [Lexgion(compute)] (no domain) should also match Python compute.

cat > /tmp/pinsight_trace_config.txt <<'EOF'
[Lexgion(Python).default]
    max_num_traces = 0

[Lexgion(compute)]
    max_num_traces = 5
EOF

session_start
PINSIGHT_TRACE_CONFIG_FILE=/tmp/pinsight_trace_config.txt \
    run_cmd "$SCRIPT_DIR/test_named_lexgion.py" $N
session_stop
COMPUTE=$(count_begin "compute")
session_destroy

check "4.6 domain-agnostic [Lexgion(compute)] matches Python compute, max=5" "$COMPUTE" 5 eq

# ═══════════════════════════════════════════════════════════════════════════
echo ""
echo "===== Phase 4.7: SIGUSR1 reload re-resolves named config ====="
echo ""
# ═══════════════════════════════════════════════════════════════════════════
# First batch with max=5, then reload with max=10.
# After reload, compute should accept up to 10 more traces.

cat > /tmp/pinsight_phase4_cfg_v1.txt <<'EOF'
[Lexgion(Python:compute)]
    max_num_traces = 5
EOF

cat > /tmp/pinsight_phase4_cfg_v2.txt <<'EOF'
[Lexgion(Python:compute)]
    max_num_traces = 10
EOF

cat > /tmp/test_phase4_reload.py <<'PYEOF'
import os, sys, signal, time, shutil

def compute(i):
    pass

N = 30
for i in range(N):
    compute(i)

# Reload with new max
shutil.copy("/tmp/pinsight_phase4_cfg_v2.txt",
            "/tmp/pinsight_phase4_cfg_v1.txt")
os.kill(os.getpid(), signal.SIGUSR1)
time.sleep(0.3)

for i in range(N):
    compute(i)
PYEOF

session_start
PINSIGHT_TRACE_CONFIG_FILE=/tmp/pinsight_phase4_cfg_v1.txt \
    run_cmd /tmp/test_phase4_reload.py
COMPUTE=$(session_stop; count_begin "compute")
session_destroy

# First batch: 5. After reload counter resets: up to 10 more. Total: 5+10=15
# After reload max changes 5→10: trace_counter stays at 5, so 5 more
# traces are emitted (5→10). Total = 5 (first batch) + 5 (second batch) = 10.
check "4.7 SIGUSR1 reload re-matches named lexgion (expect 10)" "$COMPUTE" 10 eq

# ── Cleanup ──────────────────────────────────────────────────────────────
rm -f /tmp/pinsight_trace_config.txt \
      /tmp/pinsight_phase4_cfg_v1.txt \
      /tmp/pinsight_phase4_cfg_v2.txt \
      /tmp/test_phase4_reload.py \
      /tmp/test_4_4_main.py

echo ""
echo "=========================================="
echo "Phase 4 Results: $PASS passed, $FAIL failed"
echo "=========================================="
[[ $FAIL -eq 0 ]]
