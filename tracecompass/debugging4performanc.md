## Debugging for performance

### Goals: when a program stops at the breakpoints, the debugger will analyze traces collected and show the tracecompass view. 
Essentially it is the integration of LTTng + tracecompass + debugger (editor)
* Need to make sure lttng is able to collect all traces and dump them for analysis
* Need to improve the debugger to bring the tracecompass view to the PTP perspective. 
* PTP can be used for remote debugging, but we can start with local debugging with standard CDT program and then migrate to PTP. 


### Configuring LULESH and Eclipse debuger to connect a running LULESH process
1. LULESH needs to be build with clang, modify Makefile to have 'MPICXX = OMPI_CC=clang OMPI_CXX=clang++ mpicxx -DUSE_MPI=1'
2. [ptrace needs to be disabled in kernel](https://stackoverflow.com/questions/19215177/how-to-solve-ptrace-operation-not-permitted-when-trying-to-attach-gdb-to-a-pro) so you can attach a Eclipse debugger with a running LULESH process that is launched by the trace.sh script. 
3. Need to configure in Eclipse to launch the debug session directly for LULESH when the LTTNG/PInsight are all set. 

### Related work
1. [Implementing Debug behavior in the Eclipse IDE - Tutorial](https://www.vogella.com/tutorials/EclipseDebugFramework/article.html)
