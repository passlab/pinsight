
export OMP_BASE_PATH = /opt/openmp-install
export OMP_LIB_PATH = /opt/openmp-install/lib
export PINSIGHT_LIB_PATH = $(dir $(lastword $(MAKEFILE_LIST)))lib
