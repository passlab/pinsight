LLVM_INSTALL=/home/yshi10/llvm-project/install
helloomp: helloomp.c
	clang -fopenmp -I. -I${LLVM_INSTALL}/include -L${LLVM_INSTALL}/lib -g $^ -o $@ -lm -lomp

targetomp:targetomp.c
	clang -fopenmp -fopenmp-targets=nvptx64-nvidia-cuda  -I. -I${LLVM_INSTALL}/include -L${LLVM_INSTALL}/lib -g $^ -o $@ -lm -lomp

target:target.c
	clang -fopenmp -fopenmp-targets=nvptx64-nvidia-cuda  -I. -I${LLVM_INSTALL}/include -L${LLVM_INSTALL}/lib -g $^ -o $@ -lm -lomp

task:task.c
	clang -fopenmp -I. -I${LLVM_INSTALL}/include -L${LLVM_INSTALL}/lib -g $^ -o $@ -lm -lomp
clean:
	rm helloomp 
	rm targetomp
