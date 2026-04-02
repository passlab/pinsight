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
#                 /usr/lib/llvm-21/lib ./vecadd_pinsight 5
#   bash verify_trace.sh /tmp/cuda_traces

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
echo "--- Correlation sanity: callback and activity share correlationId=3 ---"
# correlationId=3 is the first kernel launch (after 2 H2D memcpies) in vecadd
check "KernelLaunch_begin correlationId=3" "cudaKernelLaunch_begin.*correlationId = 3"
check "KernelActivity     correlationId=3" "cudaKernelActivity.*correlationId = 3"

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
