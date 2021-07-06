## Information for experimenting with [SPEC OMP 2012.1.1](https://www.spec.org/omp2012/)
* SPEC OMP 2012 paper at IWOMP'12 [SPEC OMP2012 — An Application Benchmark Suite for Parallel Systems Using OpenMP](https://link.springer.com/chapter/10.1007/978-3-642-30961-8_17)

## Config file [Ubuntu-linux-x86_64-gcc-gfortran.cfg](Ubuntu-linux-x86_64-gcc-gfortran.cfg) 
* Needs to make sure Fortran compilation link with LLVM OpenMP runtime when using gfortran, thus not GOMP. To do this, we need the following line in the cfg
      
        FOPTIMIZE = -O3 -fopenmp -mcmodel=medium -L/opt/llvm-install/llvm-20210620/lib -lomp
    
* Needs to add the trace.sh script and launching of the application in the config file. We will need to use the `submit` setting in the cfg file, see below:

        submit = /home/yyan7/tools/pinsight/scripts/trace.sh /home/yyan7/benchmarks/spec_omp2012-1.1/install/traces/md md /home/yyan7/tools/pinsight/build/libpinsight.so /opt/llvm-install/llvm-20201103/lib  $command
        
    However, there are two problem right now. 1) We have to hardcode the traces destination folder and trace name, thus we can do only one benchmark test a time. To do each benchmark test, we need to change the `submit`, and run the benchmark each time. 2) Some benchmark such as imagick runs multiple commands in each benchmarking test, thus the traces will be overwritten. At the end, on the traces for the last command are saved. 


