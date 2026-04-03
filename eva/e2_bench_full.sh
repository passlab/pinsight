#!/bin/bash
# E2 Castro Sedov 3D Overhead Benchmark — all modes
# 4×A100, 128^3 base + 2 AMR levels, 200 steps

SEDOV=/home/yyan7/tools/pinsight/eva/Castro/Exec/hydro_tests/Sedov
BIN=$SEDOV/Castro3d.gnu.MPI.CUDA.ex
INPUT=$SEDOV/inputs.3d.e2eval
LIB=/home/yyan7/tools/pinsight/build/libpinsight.so
LLVMLIB=/usr/lib/llvm-21/lib
CUDALIB=/usr/local/cuda/lib64
EVA=/home/yyan7/tools/pinsight/eva

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

echo "=== OFF MODE RUN 1 ===" ; run_pinsight $EVA/e2_off.install
echo "=== OFF MODE RUN 2 ===" ; run_pinsight $EVA/e2_off.install

echo "=== RATE-LIMITED (50 traces/lexgion) RUN 1 ===" ; run_pinsight $EVA/e2_rate_limited.install
echo "=== RATE-LIMITED (50 traces/lexgion) RUN 2 ===" ; run_pinsight $EVA/e2_rate_limited.install

echo "=== CUDA-ONLY SELECTIVE RUN 1 ===" ; run_pinsight $EVA/e2_cuda_only.install
echo "=== CUDA-ONLY SELECTIVE RUN 2 ===" ; run_pinsight $EVA/e2_cuda_only.install

echo "=== ALL DONE ==="
