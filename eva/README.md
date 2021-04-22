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

