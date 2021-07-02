## Information for experimenting with [SPEC OMP 2012.1.1](https://www.spec.org/omp2012/)
* SPEC OMP 2012 paper at IWOMP'12 [SPEC OMP2012 — An Application Benchmark Suite for Parallel Systems Using OpenMP](https://link.springer.com/chapter/10.1007/978-3-642-30961-8_17)

## Config file [Ubuntu-linux-x86_64-gcc-gfortran.cfg](Ubuntu-linux-x86_64-gcc-gfortran.cfg) 
* needs to make sure Fortran compilation link with LLVM OpenMP runtime when using gfortran, thus not GOMP. 
* needs to add the trace.sh script and launching of the application in the config file. 

