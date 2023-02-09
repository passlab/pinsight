default:
	gfortran -c -g -fopenmp hello.f90
	gcc hello.o -g -L/opt/llvm-install/llvm-20210620/lib -lomp -lgfortran -o hello

clean:
	rm -rf *.o a.out hello
