#!/bin/bash
# E2 Castro Sedov 3D Overhead Benchmark — optimized PInsight
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
CASTRO_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
PINSIGHT_ROOT="$(cd "$CASTRO_DIR/../.." && pwd)"

SEDOV=$CASTRO_DIR/source/Exec/hydro_tests/Sedov
BIN=$SEDOV/Castro3d.gnu.MPI.CUDA.ex
INPUT=$SEDOV/inputs.3d.e2eval
LIB=$PINSIGHT_ROOT/build/libpinsight.so
LLVMLIB=/usr/lib/llvm-21/lib
CUDALIB=/usr/local/cuda/lib64

cd $SEDOV

run_baseline() {
    mpirun -np 4 $BIN $INPUT 2>&1 | grep -E "Run time"
}

run_pinsight() {
    local config=$1
    if [ -n "$config" ]; then
        PINSIGHT_TRACE_CONFIG_FILE=$config \
        LD_PRELOAD=$LIB LD_LIBRARY_PATH=$LLVMLIB:$CUDALIB:$LD_LIBRARY_PATH \
            mpirun -np 4 $BIN $INPUT 2>&1 | grep -E "Run time"
    else
        LD_PRELOAD=$LIB LD_LIBRARY_PATH=$LLVMLIB:$CUDALIB:$LD_LIBRARY_PATH \
            mpirun -np 4 $BIN $INPUT 2>&1 | grep -E "Run time"
    fi
}

echo "=== BASELINE RUN 1 ===" ; run_baseline
echo "=== BASELINE RUN 2 ===" ; run_baseline

echo "=== OFF MODE RUN 1 ===" ; run_pinsight $SCRIPT_DIR/e2_off.install
echo "=== OFF MODE RUN 2 ===" ; run_pinsight $SCRIPT_DIR/e2_off.install

echo "=== RATE-LIMITED (50) RUN 1 ===" ; run_pinsight $SCRIPT_DIR/e2_rate_limited.install
echo "=== RATE-LIMITED (50) RUN 2 ===" ; run_pinsight $SCRIPT_DIR/e2_rate_limited.install

echo "=== CUDA-ONLY RUN 1 ===" ; run_pinsight $SCRIPT_DIR/e2_cuda_only.install
echo "=== CUDA-ONLY RUN 2 ===" ; run_pinsight $SCRIPT_DIR/e2_cuda_only.install

echo "=== FULL TRACING RUN 1 ===" ; run_pinsight
echo "=== FULL TRACING RUN 2 ===" ; run_pinsight

echo "=== ALL DONE ==="
