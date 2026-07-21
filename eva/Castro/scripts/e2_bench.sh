#!/bin/bash
# E2 Castro Sedov 3D Overhead Benchmark — baseline vs full tracing
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

echo "=== BASELINE RUN 1 ==="
mpirun -np 4 $BIN $INPUT 2>&1 | grep -E "Run time|zones"
echo "=== BASELINE RUN 2 ==="
mpirun -np 4 $BIN $INPUT 2>&1 | grep -E "Run time|zones"

echo "=== PINSIGHT TRACING RUN 1 ==="
LD_PRELOAD=$LIB LD_LIBRARY_PATH=$LLVMLIB:$CUDALIB:$LD_LIBRARY_PATH \
    mpirun -np 4 $BIN $INPUT 2>&1 | grep -E "Run time|zones"
echo "=== PINSIGHT TRACING RUN 2 ==="
LD_PRELOAD=$LIB LD_LIBRARY_PATH=$LLVMLIB:$CUDALIB:$LD_LIBRARY_PATH \
    mpirun -np 4 $BIN $INPUT 2>&1 | grep -E "Run time|zones"

echo "=== DONE ==="
