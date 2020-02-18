## Evaluation in terms functionality and overhead with other performance tools

### Measurement evaluation
To compare with existing performance measurement tools, including Score-P, TAU, HPCToolkit, Intel VTune, and ARM MAP. 
We will collect the following metrics between PInsight with those tools using LULESH/other proxy-app, Jacobi, NPB/spec omp, 
 * full-trace overhead, 
 * dynamic-trace overhead, 
 * full-stack trace overhead, 
 * trace file sizes. 

### Hardware and Software Setup (fornax)
 * Intel(R) Xeon(R) CPU E5-2699 v3 @ 2.30GHz, Haswell, 256 GRAM, 2 CPUs for total 36 cores. 
 * Ubuntu 18.04.3 LTS, Linux kernel 4.15.0-76
 * gcc/g++/gfortran: 7.4.0
 * CUDA version 10.0
 * Score-p 6.0, TAU 2.29
 * clang version 6.0.0
 
#### Score-p installation
`../scorep-6.0/configure --prefix=$HOME/tools/scorep-6.0-install --with-nocross-compiler-suite=clang`
