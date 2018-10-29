OMP_INSTALL=/opt/openmp-install

jacobi: jacobi.c
	gcc -fopenmp -I${OMP_INSTALL}/include -g $^ -o $@ -lm

objdump:
	objdump -d jacobi > jacobi.objdump

clean:
	rm jacobi
