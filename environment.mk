
export OMP_BASE_PATH = /home/yanyh/tools/llvm-openmp-install
export OMP_LIB_PATH = ${OMP_BASE_PATH}/lib
export PINSIGHT_LIB_PATH = $(dir $(lastword $(MAKEFILE_LIST)))lib
