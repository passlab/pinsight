LLVM_INSTALL=/home/yanyh/tools/llvm-openmp-install
helloomp: helloomp.c
	clang -fopenmp -I. -I${LLVM_INSTALL}/include -L${LLVM_INSTALL}/lib -g $^ -o $@ -lm -lomp

clean:
	rm helloomp
