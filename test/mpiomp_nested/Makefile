include ../environment.mk

# Was hardcoded to /home/yanyh/tools/llvm-openmp-install/lib/libomp.so, i.e. one
# developer's home directory -- unbuildable anywhere else.  Use the shared
# CLANG/OMP_LIB_PATH from environment.mk instead.
default:
	OMPI_CC=$(CLANG) mpicc test.c -o test -fopenmp $(OMP_LIB_PATH)/libomp.so
