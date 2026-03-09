# libomp.so path
export OMP_LIB_PATH = /usr/lib/llvm-21/lib
# omp.h and ompt.h path
export OMP_BASE_PATH = /usr/lib/llvm-21
# path of libpinsight.so, default ../build
export PINSIGHT_LIB_PATH = $(abspath $(dir $(lastword $(MAKEFILE_LIST)))../build)
