#!/usr/bin/env bash
# verify_trace.sh — Sanity-check a PInsight HIP/ROCm trace directory.
#
# Usage:
#   bash verify_trace.sh <trace_dir>
#
# Runs babeltrace2 against the trace and checks that all expected HIP
# event types are present.  Returns exit code 0 if all checks pass.
#
# Typical workflow:
#   make test                        # collect traces into vecadd_hip_traces/
#   bash verify_trace.sh vecadd_hip_traces

set -euo pipefail

TRACE_DIR="${1:-}"
if [[ -z "$TRACE_DIR" ]]; then
    echo "Usage: $0 <trace_dir>" >&2
    exit 1
fi

if ! command -v babeltrace2 &>/dev/null; then
    echo "ERROR: babeltrace2 not found in PATH" >&2
    exit 1
fi

PASS=0
FAIL=0

check() {
    local label="$1"
    local pattern="$2"
    if babeltrace2 "$TRACE_DIR" 2>/dev/null | grep -q "$pattern"; then
        echo "  PASS  $label"
        ((PASS++)) || true
    else
        echo "  FAIL  $label   (pattern: '$pattern')"
        ((FAIL++)) || true
    fi
}

echo ""
echo "=== PInsight HIP/ROCm trace verification: $TRACE_DIR ==="
echo ""

echo "--- Library lifecycle ---"
check "enter_pinsight"              "enter_pinsight"
check "exit_pinsight"               "exit_pinsight"

echo ""
echo "--- Callback API events (CPU-side, from ROCTracer Callback API) ---"
check "hipKernelLaunch_begin"       "hipKernelLaunch_begin"
check "hipKernelLaunch_end"         "hipKernelLaunch_end"
check "hipMemcpy_begin  (HtoD)"     "hipMemcpy_begin"
check "hipMemcpy_end    (HtoD)"     "hipMemcpy_end"
check "hipDeviceSync_begin"         "hipDeviceSync_begin"
check "hipDeviceSync_end"           "hipDeviceSync_end"

echo ""
echo "--- Activity API events (GPU-side timestamps, from ROCTracer Activity API) ---"
check "hipMemcpyActivity  (GPU timing)"  "hipMemcpyActivity"
check "hipKernelActivity  (GPU timing)"  "hipKernelActivity"

echo ""
echo "--- Clock calibration (ROCTracer epoch vs CLOCK_MONOTONIC) ---"
check "hip_clock_calibration"       "hip_clock_calibration"

echo ""
echo "--- Field content checks ---"
check "correlation_id field present"    "correlation_id ="
check "begin_ns field present"          "begin_ns ="
check "end_ns field present"            "end_ns ="
check "kernel_name field present"       "kernel_name ="
check "bytes field present"             "bytes ="
check "hip_func field present"          "hip_func ="
check "devId field present"             "devId ="

echo ""
echo "--- Kernel name check ---"
check "VecAdd kernel name captured"     "VecAdd\|vecAdd\|_Z6VecAdd\|_Z6vecAdd"

echo ""
echo "--- Correlation sanity: callback and activity share same correlation_id ---"
# In vecadd with 5 iters: first kernel is corr_id 3 (after 2 HtoD memcpies)
check "KernelLaunch_begin has correlation_id" \
      "hipKernelLaunch_begin.*correlation_id = [0-9]"
check "KernelActivity     has correlation_id" \
      "hipKernelActivity.*correlation_id = [0-9]"

echo ""
echo "--- Rate control: multiple iterations traced ---"
KERNEL_COUNT=$(babeltrace2 "$TRACE_DIR" 2>/dev/null | grep -c "hipKernelLaunch_begin" || true)
echo "  hipKernelLaunch_begin count: $KERNEL_COUNT  (expected 5 for 5 iterations)"
if [[ "$KERNEL_COUNT" -eq 5 ]]; then
    echo "  PASS  kernel launch count matches iteration count"
    ((PASS++)) || true
else
    echo "  FAIL  expected 5 kernel launches, got $KERNEL_COUNT"
    ((FAIL++)) || true
fi

echo ""
echo "--- Summary ---"
echo "  Passed: $PASS"
echo "  Failed: $FAIL"
echo ""

if [[ $FAIL -eq 0 ]]; then
    echo "  ALL CHECKS PASSED"
    exit 0
else
    echo "  $FAIL CHECK(S) FAILED"
    exit 1
fi
