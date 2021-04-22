include ../environment.mk

helloomp: helloomp.c
	clang -fopenmp -g $^ -o $@ -lm

objdump:
	objdump -d helloomp > helloomp.objdump

test:
	LD_PRELOAD=$(OMP_LIB_PATH)/libomp.so:$(PINSIGHT_LIB_PATH)/libpinsight.so ./helloomp

clean:
	rm helloomp
