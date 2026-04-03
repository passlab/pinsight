#!/bin/bash
SEDOV=/home/yyan7/tools/pinsight/eva/Castro/Exec/hydro_tests/Sedov
BIN=$SEDOV/Castro3d.gnu.MPI.CUDA.ex
INPUT=$SEDOV/inputs.3d.e2eval
LIB=/home/yyan7/tools/pinsight/build/libpinsight.so
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
