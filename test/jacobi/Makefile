include ../environment.mk

CC = clang-21

jacobi: jacobi.c
	$(CC) -fopenmp -g -O3 $^ -o $@ -lm

objdump:
	objdump -d jacobi > jacobi.objdump

test:
	OMP_TOOL_LIBRARIES=$(PINSIGHT_LIB_PATH)/libpinsight.so ./jacobi 256 256

clean:
	rm -f jacobi
