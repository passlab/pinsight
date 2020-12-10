LLVM_INSTALL=/home/xinyao/tools/llvm-openmp-install
task: task.c
	clang -fopenmp -I. -I${LLVM_INSTALL}/include -L${LLVM_INSTALL}/lib -g $^ -o $@ -lm -lomp
	
task_depend: task_depend.c
	clang -fopenmp -I. -I${LLVM_INSTALL}/include -L${LLVM_INSTALL}/lib -g $^ -o $@ -lm -lomp

clean:
	rm task
	rm task_depend
