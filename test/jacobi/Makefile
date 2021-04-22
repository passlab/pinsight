include ../environment.mk

jacobi: jacobi.c
	#clang -fopenmp -g -finstrument-functions -I$(OMP_BASE_PATH)/include -g $^ -o $@ -lm
	clang -fopenmp -g -I$(OMP_BASE_PATH)/include -g $^ -o $@ -lm

objdump:
	objdump -d jacobi > jacobi.objdump

test:
	LD_PRELOAD=$(OMP_LIB_PATH)/libomp.so:$(PINSIGHT_LIB_PATH)/libpinsight.so ./jacobi 2048 2048

clean:
	rm jacobi
