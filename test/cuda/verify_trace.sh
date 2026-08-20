#!/usr/bin/env bash
# verify_trace.sh — Sanity-check a PInsight CUDA trace directory.
#
# Usage:
#   bash verify_trace.sh <trace_dir>
#
# Runs babeltrace2 against the trace and checks that all expected CUDA
# event types are present.  Returns exit code 0 if all checks pass,
# non-zero otherwise.
#
# Typical workflow:
#   bash trace.sh /tmp/cuda_traces my_session /path/to/libpinsight.so \
#                 /usr/lib/llvm-22/lib ./vecadd_pinsight 5
#   bash verify_trace.sh /tmp/cuda_traces

set -euo pipefail

TRACE_DIR="${1:-}"
if [[ -z "$TRACE_DIR" ]]; then
    echo "Usage: $0 <trace_dir>" >&2
    exit 1
fi

# babeltrace2 preferred; babeltrace 1.x reads the same CTF traces.
BT=$(command -v babeltrace2 || command -v babeltrace) || {
    echo "ERROR: neither babeltrace2 nor babeltrace found in PATH" >&2
    exit 1
}

PASS=0
FAIL=0

# NOTE: use grep -c, never grep -q, here.  `grep -q` exits at the first match,
# which SIGPIPEs the reader; under `set -o pipefail` the pipeline then reports
# 141 and the check reads as a failure.  Patterns occurring EARLY in the stream
# fail that way while late ones pass — which silently broke every host-callback
# check in this script.  `grep -c` consumes the whole stream, so the reader
# exits 0.  Counting also makes the output more informative than a bare PASS.
check() {
    local label="$1"
    local pattern="$2"
    local n
    n=$("$BT" "$TRACE_DIR" 2>/dev/null | grep -c "$pattern" || true)
    if [[ "$n" -gt 0 ]]; then
        echo "  PASS  $label  ($n)"
        ((PASS++)) || true
    else
        echo "  FAIL  $label   (pattern: '$pattern')"
        ((FAIL++)) || true
    fi
}

echo ""
echo "=== PInsight CUDA trace verification: $TRACE_DIR ==="
echo ""

echo "--- Callback API events (CPU-side, from CUPTI Callback API) ---"
check "cudaKernelLaunch_begin"          "cudaKernelLaunch_begin"
check "cudaKernelLaunch_end"            "cudaKernelLaunch_end"
check "cudaMemcpy_begin  (sync)"        "cudaMemcpy_begin"
check "cudaMemcpy_end    (sync)"        "cudaMemcpy_end"
check "cudaDeviceSync_begin"            "cudaDeviceSync_begin"
check "cudaDeviceSync_end"              "cudaDeviceSync_end"

echo ""
echo "--- Activity API events (GPU-side timestamps, from CUPTI Activity API) ---"
check "cudaMemcpyActivity  (GPU timing)" "cudaMemcpyActivity"
check "cudaKernelActivity  (GPU timing)" "cudaKernelActivity"

echo ""
echo "--- Clock calibration (CUPTI epoch vs CLOCK_MONOTONIC offset) ---"
check "cuda_clock_calibration"           "cuda_clock_calibration"

echo ""
echo "--- Field content checks ---"
check "start_gpu field present"          "start_gpu ="
check "end_gpu field present"            "end_gpu ="
check "correlationId field present"      "correlationId ="
check "cudaMemcpyKind enum decoded"      "cudaMemcpyHostToDevice\|cudaMemcpyDeviceToHost"
check "clock_monotonic_ns field"         "clock_monotonic_ns ="
check "cupti_timestamp_ns field"         "cupti_timestamp_ns ="

echo ""
echo "--- Correlation sanity: every kernel launch has a matching activity record ---"
# Do NOT hardcode a correlationId: the value depends on which events are enabled
# (e.g. tracing cudaMalloc shifts every later id).  The real invariant is that
# the SET of correlationIds on the host launches equals the set on the GPU
# activity records — that is what makes CB<->ACT correlation usable downstream.
ids() {
    "$BT" "$TRACE_DIR" 2>/dev/null | grep "$1" \
        | grep -o 'correlationId = [0-9]*' | awk '{print $3}' | sort -un
}
launch_ids=$(ids cudaKernelLaunch_begin)
act_ids=$(ids cudaKernelActivity)
n_launch=$(printf '%s\n' "$launch_ids" | grep -c . || true)
if [[ "$n_launch" -gt 0 && "$launch_ids" == "$act_ids" ]]; then
    echo "  PASS  launch/activity correlationId sets match  ($n_launch ids)"
    ((PASS++)) || true
else
    echo "  FAIL  launch/activity correlationId sets differ"
    echo "          launches: $(printf '%s ' $launch_ids)"
    echo "          activity: $(printf '%s ' $act_ids)"
    ((FAIL++)) || true
fi

echo ""
echo "--- Summary ---"
echo "  Passed: $PASS"
echo "  Failed: $FAIL"
echo ""

if [[ $FAIL -eq 0 ]]; then
    echo "  ALL CHECKS PASSED ✓"
    exit 0
else
    echo "  $FAIL CHECK(S) FAILED ✗"
    exit 1
fi
