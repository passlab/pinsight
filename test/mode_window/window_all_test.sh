#!/bin/bash
# GPU-free end-to-end test for window_timeout + window_end_trigger (first/all).
# Exercises the domain-agnostic core (control-thread window timer + the 'all'
# count-policy gate) via OpenMP/OMPT — no GPU, no flux, no lttng session needed
# (the auto-trigger logic runs off trace counters regardless of any trace sink;
# we assert on PInsight's stderr control messages).
#
# Build + run:  ./window_all_test.sh   (env: PINSIGHT_LIB, OMP_LIB)
set -u
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
PINSIGHT_LIB=${PINSIGHT_LIB:-$ROOT/build_omp/libpinsight.so}
OMP_LIB=${OMP_LIB:-$(clang-21 -print-file-name=libomp.so 2>/dev/null)}
[ -e "$OMP_LIB" ] || OMP_LIB=/lib64/libomp.so
BIN=$(dirname "$0")/mode_test
CFG=$(mktemp /tmp/win_all.XXXX.txt)
PRE="$OMP_LIB:$PINSIGHT_LIB"
fails=0

[ -e "$PINSIGHT_LIB" ] || { echo "missing $PINSIGHT_LIB (build with PINSIGHT_OPENMP=TRUE)"; exit 1; }
clang-21 -fopenmp -g "$(dirname "$0")/mode_test.c" -o "$BIN" -lm || { echo "compile failed"; exit 1; }

run(){ env LD_PRELOAD="$PRE" PINSIGHT_TRACE_CONFIG_FILE="$CFG" "$BIN" "$@" 2>&1; }
# iteration after which the synchronous immediate (count) auto-trigger printed
fired_iter(){ echo "$1" | awk '/Auto-trigger \(immediate\)/{print p; exit} /iter [0-9]+ done/{p=$2}'; }
check(){ if [ "$2" = "$3" ]; then echo "  [PASS] $1 ($2)"; else echo "  [FAIL] $1: got '$2' expected '$3'"; fails=$((fails+1)); fi; }

cfg_count(){ cat > "$CFG" <<EOF
[OpenMP.global]
    trace_mode = TRACING
[Lexgion.default]
    max_num_traces = 3
    window_end_trigger = $1
    window_end_action = OpenMP:MONITORING
EOF
}

# fired_iter reports the last COMPLETED iter before the trigger line, i.e. the
# trigger fired during iter (N+1): 'first' caps region A on its 3rd call (iter 2,
# after "iter 1 done" -> 1); 'all' caps the slow region C on its 3rd call (iter
# 20, after "iter 19 done" -> 19). The gap (1 vs 19) is the whole point.
echo "== T1: window_end_trigger=first fires on the FAST region (early) =="
cfg_count first; o=$(run 30 0 10)
check "first fires during iter 2 (fast region A caps)" "$(fired_iter "$o")" "1"
check "exactly one switch" "$(echo "$o"|grep -c 'OpenMP mode -> MONITORING')" "1"

echo "== T2: window_end_trigger=all waits for the SLOW region (late) =="
cfg_count all; o=$(run 30 0 10)
check "all fires during iter 20 (slow region C caps)" "$(fired_iter "$o")" "19"
check "exactly one switch" "$(echo "$o"|grep -c 'OpenMP mode -> MONITORING')" "1"

echo "== T3: window_timeout standalone (no count cap) ends the window by time =="
cat > "$CFG" <<EOF
[OpenMP.global]
    trace_mode = TRACING
[Lexgion.default]
    window_timeout = 2
    window_end_action = OpenMP:MONITORING
EOF
o=$(run 100 50 10)
check "window_timeout fired once"   "$(echo "$o"|grep -c 'window_timeout (')" "1"
check "no count trigger (standalone)" "$(echo "$o"|grep -c 'Auto-trigger (immediate)')" "0"
check "exactly one switch"          "$(echo "$o"|grep -c 'OpenMP mode -> MONITORING')" "1"

echo "== T4: 'all' backstop — slow region NEVER caps, window_timeout closes it =="
cat > "$CFG" <<EOF
[OpenMP.global]
    trace_mode = TRACING
[Lexgion.default]
    max_num_traces = 3
    window_end_trigger = all
    window_timeout = 2
    window_end_action = OpenMP:MONITORING
EOF
o=$(run 60 50 1000)   # C called only at iter 0 -> never reaches 3
check "all never fires (never-caps)"   "$(echo "$o"|grep -c 'Auto-trigger (immediate)')" "0"
check "window_timeout backstop fired"  "$(echo "$o"|grep -c 'window_timeout (')" "1"

echo "== T5: count wins the race -> timer must NO-OP (no double fire) =="
cat > "$CFG" <<EOF
[OpenMP.global]
    trace_mode = TRACING
[Lexgion.default]
    max_num_traces = 3
    window_end_trigger = first
    window_timeout = 3
    window_end_action = OpenMP:MONITORING
EOF
o=$(run 100 50 10)    # count fires ~iter2 (<<3s); timer wakes at 3s and must no-op
check "count fired"                 "$(echo "$o"|grep -c 'Auto-trigger (immediate)')" "1"
check "window_timeout did NOT fire" "$(echo "$o"|grep -c 'window_timeout (')" "0"
check "exactly one switch"          "$(echo "$o"|grep -c 'OpenMP mode -> MONITORING')" "1"

echo "== T6: 'all' without window_timeout -> startup never-fires WARNING =="
cat > "$CFG" <<EOF
[Lexgion.default]
    max_num_traces = 3
    window_end_trigger = all
    window_end_action = OpenMP:MONITORING
EOF
o=$(run 2 0 10)
check "warning printed" "$(echo "$o"|grep -c "window_end_trigger='all' with no window_timeout")" "1"

rm -f "$CFG"
echo ""
if [ "$fails" = 0 ]; then echo "ALL PASS"; else echo "$fails CHECK(S) FAILED"; exit 1; fi
