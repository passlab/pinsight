## Evaluation in terms functionality and overhead with other performance tools

### Overhead evaluation
To compare with existing performance measurement tools, including Score-P and TAU. We may add HPCToolkit, Intel VTune, and ARM MAP. 
We will collect the following metrics between PInsight with those tools using LULESH/other proxy-app, Jacobi, NPB/spec omp, 
 * full-trace overhead as pecentage of the execution time as well as the execution time of tracing using different tools. 
 * dynamic-trace overhead, when using hybrid tracing-sampling approach in comparision with other tools
 * trace file sizes. 

### Feature demonstration and enhancement
1. Figures to shows LULESH timegraph, and 3D graph per-thread summary for OpenMP program of both CPU and GPU offloading
3. The LLVM OpenMP runtime that has the most recent OMPT inmplementation: https://github.com/OpenMPToolsInterface/LLVM-openmp/
4. OpenMP tasking support with OMPT and PInsight. 

### Future feature enhancement
1. For GPU/CUDA visualization, show some features similar to NVVP, but in 2D/3D view. 
1. For MPI, shows p2p and collective message passing calls.  

### Hardware and Software Setup
Check [fornax.md](fornax.md) file

### LULESH
Change in Makefile CXX compiler. For all the compilation, `-fopenmp, -g -O3` is the compiler flag, 32 threads are used by setting `OMP_NUM_THREADS=32`
1. Standard compilation, use clang++ 12.0.0, MPI is disabled. This version is used for PInsight evaluation and baseline with no tool or tracing

       CXX = clang++ -DUSE_MPI=0 //In Makefile
       make  # produce lulesh2.0 executable
       ./lulesh2.0   //baseline
       export PINSIGHT_LEXGION_TRACE_CONFIG=src/lexgion_trace_config_LULESH_selective.txt # Setting lexgion trace config
       ./scripts/trace.sh ./traces/LULESH LULESH build/libpinsight.so /opt/llvm/clang+llvm-12.0.0-x86_64-linux-gnu-ubuntu-16.04/ eva/proxyapps/LULESH/lulesh2.0 // PInsight evaluation. The LULESH itself output execution time info. 
       change the configuration and run the trace.sh again to collect data
       df -u traces/LULESH  # check trace file size
  
2.  score-p tracing compilation 

        CXX = scorep --openmp --thread=omp --keep-files clang++ -DUSE_MPI=0
        make  # produce lulesh2.0 executable
        export SCOREP_ENABLE_TRACING=true   # enable score-p tracing
        export SCOREP_TOTAL_MEMORY=15992000K  #Make sure buffer is bigger enough, max 4G
        ./lulesh2.0   #score-p version
        The execution produce a folder that contains traces and other info. use `du -h` command to check the trace file size. 
        
3. HPCToolkit sampling 
     1. Follow HPCToolkit installation with spack: http://hpctoolkit.org/software-instructions.html
     
     
              git clone https://github.com/spack/spack.git
              git clone https://github.com/hpctoolkit/hpctoolkit.git
              .  /path/to/spack/share/spack/setup-env.sh
              cd spack/etc/spack
              cp defaults/config.yaml .
              vi config.yaml
              vi ~/.spack/linux/compilers.yaml and add /usr/bin/gfortran to f77 and fc compiler, otherwise, PAPI won't install
              spack spec hpctoolkit
              spack install hpctoolkit //take a long time
              #set hpctookit path to PATH
              export PATH=/scratch/tmp/spack/opt/spack/linux-ubuntu18.04-nehalem/gcc-9.3.0/hpctoolkit-2021.03.01-mqum45fktw6nhpy57ggrsjhjq5tohiuh/bin:$PATH
              #go to LULESH folder and follow instruction from http://hpctoolkit.org/man/hpctoolkit.html#section_2
              hpcrun  -e CYCLES ./lulesh2.0-fresh 
              hpcrun  ./lulesh2.0-fresh 
              hpcrun  -t ./lulesh2.0-fresh 
              du -h hpctoolkit-lulesh2.0-fresh-measurements
              hpcstruct ./lulesh2.0-fresh
              hpcprof -I ./+ -S ./lulesh2.0-fresh.hpcstruct 
              


              
              
                     
                     
     
    
