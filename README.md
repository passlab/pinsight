# PInsight: Dynamic and Asynchronous Tracing and Visualization for OpenMP, MPI and CUDA Programs

PInsight is a performance tool that help developers trace and measure the performance of the 
execution of parallel applications, and to analyze and visualize the tracing to identify performance issues. 
It includes PInsight shared library developed using LTTng UST for dynamic and asynchronous tracing, 
and Eclipse/Trace Compass plugins for analyzing and visualizing traces. 

## PInsight shared library for tracing OpenMP, MPI and CUDA programs

The PInsight trace library is a shared library called `pinsight`. The library implemnts OpenMP [OMPT][ompt] and CUDA [CUPTI][cupti] event
callbacks for tracing OpenMP and CUDA application, and MPI [PMPI][pmpi] wrapper for tracing MPI applications. 
The OMPT/CUPTI callbacks and PMPI wrappers redirect tracing to [LTTng][lttng] UST for processing tracing asynchronously. The library also
implements flexible runtime configuration of tracing options such as tracing rate, allowing 1) for optimizing dynamic tracing 
to reduce unnecessnary and redundant tracing, 2) for runtime reconfiguration for both tracing and application for runtime performance optimization, and 3)
for performance debugging such that the application execution can be paused for performance analysis and optimization, and then resumed with optimized execution. 

   [lttng]: https://lttng.org
   [ompt]: https://www.openmp.org/wp-content/uploads/ompt-tr.pdf
   [pmpi]: https://www.open-mpi.org/faq/?category=perftools#PMPI
   [cupti]: https://docs.nvidia.com/cuda/cupti/index.html
   
### Build
1. Install the essential dependencies. On Ubuntu systems:

           sudo apt-get install build-essential cmake git

2. Then install LTTng, which has some special [setup instructions][lttng-install].

   [lttng-install]: https://lttng.org/docs

           sudo apt-get install lttng-tools lttng-modules-dkms liblttng-ust-dev babeltrace

3. Clone the repo and build the `pinsight` shared library. It uses the cmake and make utilities. 
The option to enable features of PInsight includes:

            option(PINSIGHT_OPENMP       "Build with OpenMP support"       TRUE)
            option(PINSIGHT_MPI          "Build with MPI support"          FALSE)
            option(PINSIGHT_CUDA         "Build with CUDA support"         TRUE)
            option(PINSIGHT_ENERGY       "Build with Energy tracing"       FALSE)
            option(PINSIGHT_BACKTRACE    "Build with Backtrace enabled"    TRUE)

You can pass each option to cmake, e.g. `cmake -DPINSIGHT_MPI=TRUE | FALSE <other options> ` to turn on or off each feature. 
For OpenMP tracing, building the library requires omp.h and omp-tools.h headers. The PInsight repo `src` folder contains copies of these two header files
from Clang/LLVM 14.0. If you need to use the two headers that are already installed,  pass to cmake via `OPENMP_INCLUDE_PATH` setting. 
For CUDA tracing, which depends on CUPTI, CUDA/CUPTI installation folder should be provided via `CUDA_INSTALL` with default to be `/usr/local/cuda`. 

Example: 

           git clone https://github.com/passlab/pinsight.git
           cd pinsight && mkdir build && cd build
           cmake -DOPENMP_INCLUDE_PATH=/home/yanyh/tools/llvm-openmp-install/include ..
           make

The instructions above will result in `build/libpinsight.so` to be created. 

#### To install LLVM OpenMP library for OpenMP OMPT event tracing

To build and trace OpenMP programs which uses the OMPT event tracing library, [LLVM OpenMP runtime][llvm-openmp] needs to be installed and OMPT 
should be enabled to support runtime tracing. LLVM has combined modules into 
one github repo. If you already have Clang/LLVM installed with OpenMP/OMPT enabled, you only need to provide the path to the OpenMP headers (omp.h and omp-tools.h) for 
building the pinsight library. You will need the libomp.so library for PInsight to trace an OpenMP application. 
If you do not have Clang/LLVM installed, you will need to clone the whole repo, 
but only need to install the OpenMP library. The following is an example of building LLVM OpenMP runtime library (taken from our `Dockerfile`):

    git clone https://github.com/llvm-project/openmp.git && \
    cd openmp && \
    git remote update && \
    cd .. && \
    mkdir -p openmp-build && \
    cd openmp-build && \
    cmake -G "Unix Makefiles" \
      -DCMAKE_BUILD_TYPE=Debug \
      -DCMAKE_INSTALL_PREFIX=/home/yanyh/tools/llvm-openmp-install \
      -DOPENMP_ENABLE_LIBOMPTARGET=off \
      -DLIBOMP_OMPT_SUPPORT=on \
      ../openmp

    make && make install

   [llvm-openmp]: https://github.com/llvm/llvm-project/tree/master/openmp

#### To build and setup for CUDA CUPTI event tracing
For CUDA CUPTI tracing, `PINSIGHT_CUDA` should be set to `TRUE` for cmake. CUDA SDK with CUPTI SDK should be installed. 
On Ubuntu 22.04, to install CUDA 12.2, follow steps in the 
NVIDIA [CUDA 12.2 download page](https://developer.nvidia.com/cuda-12-2-2-download-archive), such as: 

       wget https://developer.download.nvidia.com/compute/cuda/repos/ubuntu2204/x86_64/cuda-keyring_1.1-1_all.deb
       sudo dpkg -i cuda-keyring_1.1-1_all.deb
       sudo apt-get update
       sudo apt-get -y install cuda

The above command will install CUDA at `/usr/local/cuda`, with that, the pinsight library can be built with support for CUDA as: 

           git clone https://github.com/passlab/pinsight.git
           cd pinsight && mkdir build && cd build
           cmake -DPINSIGHT_CUDA=TRUE ..
           make
	   
If CUDA is not installed at `/usr/local/cuda`, The `CUDA_INSTALL` option should pass to cmake to provide the path to a CUDA install folder. 

#### To build and setup for MPI PMPI event tracing
To be completed

### Tracing the Application Execution

#### Tracing script

In the `scripts/` directory, a script called `trace.sh` is provided.
This script helps make generating LTTng traces for MPI/OpenMP/CUDA programs easier.
OpenMP OMP_NUM_THREADS should be set for the needed number of threads for OpenMP program and
mpirun can be used as command to launch MPI applications. The instruction of using `trace.sh` is as follows: 

```
Usage for this tracing script for use with PInsight:
    trace.sh TRACEFILE_DEST TRACE_NAME PINSIGHT_LIB LD_LIBRARY_PATH_PREPEND PROG_AND_ARGS...

Arguments:
  TRACEFILE_DEST    Where to write the LTTng traces.
  TRACE_NAME	    Give a proper name for the trace to be displayed in tracecompass.  
  PINSIGHT_LIB      Full-path PInsight shared library file name to use with user application.
  LD_LIBRARY_PATH_PREPEND   A list of paths separated by :. The list is prepended to 
		    the LD_LIBRARY_PATH env. This argument can be used to provide
	            path for the libraries used by the pinsight tracing, such as
		    path for libomp.so or libmpi.so. If none is needed, e.g. the path are
		    already set in the LD_LIBRARY_PATH, : should be provided for this arg.  

Examples:
    trace.sh ./traces/jacobi jacobi \ 
      /opt/pinsight/lib/libpinsight.so /opt/llvm-install/lib \
      ./test/jacobi 2048 2048
    
    trace.sh ./traces/LULESH LULESH \ 
      /opt/pinsight/lib/libpinsight.so /opt/llvm-install/lib:/opt/openmpi-install/lib \ 
      mpirun -np 8 test/LULESH/build/lulesh2.0 -s 20

```

Example using the `jacobi` application with `8` threads:

```
yyan7@yyan7-Ubuntu:~/tools/pinsight$ export OMP_NUM_THREADS=8
yyan7@yyan7-Ubuntu:~/tools/pinsight$ ./scripts/trace.sh ./traces/jacobi jacobi ./build/libpinsight.so /home/yyan7/compiler/llvm-openmp-install/lib test/jacobi/jacobi 512
Session jacobi-tracing-session created.
Traces will be written in /home/yyan7/tools/pinsight/traces/jacobi
UST event lttng_pinsight_enter_exit:* created in channel channel0
UST event lttng_pinsight_ompt:* created in channel channel0
UST event lttng_pinsight_pmpi:* created in channel channel0
UST event lttng_pinsight_cuda:* created in channel channel0
Tracing started for session jacobi-tracing-session
Usage: jacobi [<n> <m> <alpha> <tol> <relax> <mits>]
	n - grid dimension in x direction, default: 256
	m - grid dimension in y direction, default: n if provided or 256
	alpha - Helmholtz constant (always greater than 0.0), default: 0.0543
	tol   - error tolerance for iterative solver, default: 1e-10
	relax - Successice over relaxation parameter, default: 1
	mits  - Maximum iterations for iterative solver, default: 5000
jacobi 512 512 0.0543 1e-10 1 5000
------------------------------------------------------------------------------------------------------
Total Number of Iterations: 5001
Residual: 2.35602684028891e-08
seq elasped time(ms):        13806
MFLOPS:      1224.58
Total Number of Iterations: 5001
Residual: 0.0269720666110516
OpenMP (8 threads) elasped time(ms):         4064
MFLOPS:      4160.06
Solution Error: 0.000921275
============================================================
Lexgion report from thread 0: total 13 lexgions
#	codeptr_ra	count	trace count	type	end_codeptr_ra
1	0xffffff	1		1	3	0xffffff
2	0xffffff	1		0	7	(nil)
3	0x402405	1		1	3	0x402405
4	0x402405	1		1	7	(nil)
5	0x402544	1		1	20	0x402544
6	0x40257e	1		1	23	0x40257e
7	0x4010bd	5000		50	3	0x4010bd
8	0x4010bd	5000		50	7	(nil)
9	0x4012d9	5000		50	20	0x4013c5
10	0x401176	5000		50	3	0x401176
11	0x401176	5000		50	7	(nil)
12	0x40159d	5000		50	20	0x4017c7
13	0x401803	5000		50	23	0x401803
-------------------------------------------------------------
parallel lexgions (type 3) from thread 0
#	codeptr_ra	count	trace count	type	end_codeptr_ra
1	0xffffff	1		1	3	0xffffff
2	0x402405	1		1	3	0x402405
3	0x4010bd	5000		50	3	0x4010bd
4	0x401176	5000		50	3	0x401176
-------------------------------------------------------------
sync lexgions (type 23) from thread 0
#	codeptr_ra	count	trace count	type	end_codeptr_ra
1	0x40257e	1		1	23	0x40257e
2	0x401803	5000		50	23	0x401803
-------------------------------------------------------------
work lexgions (type 20) from thread 0
#	codeptr_ra	count	trace count	type	end_codeptr_ra
1	0x402544	1		1	20	0x402544
2	0x4012d9	5000		50	20	0x4013c5
3	0x40159d	5000		50	20	0x4017c7
-------------------------------------------------------------
master lexgions (type 21) from thread 0
#	codeptr_ra	count	trace count	type	end_codeptr_ra
============================================================
Waiting for data availability.
Tracing stopped for session jacobi-tracing-session
Session jacobi-tracing-session destroyed
yyan7@yyan7-Ubuntu:~/tools/pinsight$ 

```

#### Tracing MPI or MPI+X applications, e.g. trace LULESH

     scripts/trace.sh traces/LULESH-MPI-8npX4th LULESH-MPI-8npX4th ./lib/libpinsight.so \
     /home/yanyh/tools/llvm-openmp-install:/opt/openmpi-install/lib \
     mpirun -np 8 ./test/LULESH/build/lulesh2.0 

#### Dump the trace data to text using babeltrace
After tracing complete, you can use babeltrace tools to dump the trace data to text on the terminal

     babeltrace <tracefolder>

#### Manual LTTng tracing session

If not using the provided trace.sh script, one can follow LTTng document to set up [tracing session][lttng-tracing-session], and then enable some [event rules][lttng-event-rules] before starting tracing:

   [lttng-tracing-session]: https://lttng.org/docs/v2.10/#doc-tracing-session
   [lttng-event-rules]: https://lttng.org/docs/v2.10/#doc-event

    # Create a userspace session.
    lttng create ompt-tracing-session --output=/tmp/ompt-trace

    # Create and enable event rules.
    lttng enable-event --userspace lttng_pinsight_ompt:'*'
    lttng enable-event --userspace lttng_pinsight_pmpi:'*'

    # Start LTTng tracing.
    lttng start

Once the session has started, LTTng's daemon will be ready to accept the traces our application generates.

In order to generate traces about OMPT events, our `libpinsight.so` library needs to be preloaded by the application. Latest OpenMP standard provide `OMP_TOOL_LIBRARIES`
env to point the tool library (pinsight.so), thus no need to use LD_PRELOAD. The OpenMP runtime library is also need to provided for the execution. 
In order to ensure the 2 shared libraries are loaded correctly, we use the `LD_PRELOAD` environment variable, like so:

    # Run an OpenMP application with our shared libraries.
    LD_PRELOAD=/opt/openmp-install/lib/libomp.so:/opt/pinsight/lib/libpinsight.so ./your_application

Once the application has run, we can tell LTTng to stop tracing:

    # Stop LTTng tracing.
    lttng stop
    lttng destroy

The resulting trace files will be located at `/tmp/ompt-trace`, and can be loaded into TraceCompass or Babeltrace for analysis.

#### Enable/disable debug output

On larger trace datasets, the debug printouts from PInsight can be quite massive.
By default, these printouts are disabled for sake of disk usage.

To enable the printouts, set the `PINSIGHT_DEBUG_ENABLE` environment variable to 1 before running a trace.

Shell variable example:

    export PINSIGHT_DEBUG_ENABLE=1
    # Normal tracing stuff from here on...


## Runtime configuration for optimizing dynamic tracing
In comparison with static tracing that when tracing are started, no changes can be made to change tracing rate, position, etc. 
Dynamic tracing enables highly optimized tracing according to the needs and behavior of the program execution. 
This is achieved by allowing enabling and disabling tracing in multiple granularity level. 
Currently, PInsight implements two ways for users to set the configuration options for tracing: 1) via environment variables, and 2) via a config file.

#### Configuration via env variables:
Below are the env variables and their optional values that one can use to set the runtime tracing options. Env settings 
are applied to the runtime default configuration for all lexgions. If you want lexgion-specific configuration, you have to 
use the second way which is using a config file.

          PINSIGHT_TRACE_OPENMP=TRUE|FALSE
          PINSIGHT_TRACE_MPI=TRUE|FALSE
          PINSIGHT_TRACE_CUDA=TRUE|FALSEk
          PINSIGHT_TRACE_ENERGY=TRUE|FALSE
          PINSIGHT_TRACE_BACKTRACE=TRUE|FALSE
          PINSIGHT_TRACE_RATE=<trace_starts_at>:<initial_trace_count>:<max_num_traces>:<tracing_rate>
 
The `PINSIGHT_TRACE_RATE` env can be used to specifying the tracing and sampling rate with four integers. The format is
`<trace_starts_at>:<initial_trace_count>:<max_num_traces>:<tracing_rate>`, e.g. `PINSIGHT_TRACE_RATE=10:20:100:10`.
The meaning of each of the four number can be found from the lexgion_trace_config_keys[] declaration. E.g.:
     `PINSIGHT_TRACE_RATE=0:0:10:1`, This is the system default (in `lexgion_trace_config_sysdefault` function).
                It indicates to start recording from the first execution, then record the first 0 traces,
                then after that, record one trace per 1 execution and in total max 10 traces should be recorded.
     `PINSIGHT_TRACE_RATE=0:0:-1:-1`, record all the traces.
     `PINSIGHT_TRACE_RATE=0:0:-1:10`, record 1 trace per 10 executions for all the executions.
     `PINSIGHT_TRACE_RATE=0:20:20:-1`, record the first 20 iterations
 
#### Using a config file to specify runtime tracing options
The file name can be specified using the `PINSIGHT_TRACE_CONFIG` env. Please check the sample file in the src folder.

If both ways are used by the users, the options provided by the config file will be used.

Further improvement: to completely turn off OMPT/PMPI/CUPTI such that no overhead will incur at all for the whole program execution. 
1. One approach is to use an env varabile and a global flag for setting and checking upon entry to each PInsight/OMPT/PMPI/CUPTI call, though this still introduces the overhead of making those calls and one step of checking. 
2. Another approach is to completely shutdown/finalize the OMPT/PMPI/CUPTI for the program and unload/unlink the libpinsight.so. For OpenMP, this can be accomplished by ompt_finalize call, which turn off OMPT, thus the whole PInsight is turned off and the libpinsight.so can be unloaded. For MPI, this can be accomplished by unlinking libpinsight.so. For CUPTI, **TBD**. With this approach, the program does not expect to re-enable the tracing later on. 
3. This approach can be used for enable/disable specific tracing of OpenMP, MPI or CUPTI. With this approach, we need to build three different librarys each for OpenMP/MPI/CUPTI. 
4. This approach can also be used in the debugging situation that after the debuger and performance analysis know the configuration, and set the configuration, tracing can be disabled to eliminate the overhead otherwise that would be introduced with tracing

---------------------------------------------------------------------------

## View, Analysis, and Visualize PInsight Traces

### Trace text dump
After tracing complete, you can use babeltrace tool to dump the trace data to text on the terminal

     babeltrace <tracefolder>

### Python script
This repo comes with a few Python scripts which can dump program statistics into CSV format for analysis.

#### Per-thread per-region

On a trace that had 32 threads:

    python3 python/per_thread_per_region.py /tmp/ompt-jacobi/ 32 > per-region.csv

To run event processing in parallel, use the `-j <NUM_PROCESSES>` flag:

    python3 python/per_thread_per_region.py -j 8 /tmp/ompt-jacobi/ 32 > per-region.csv

#### Whole-program per-thread

On a trace that had 32 threads:

    python3 python/per_thread.py /tmp/ompt-jacobi/ 32 > per-thread.csv

To run event processing in parallel, use the `-j <NUM_PROCESSES>` flag:

    python3 python/per_thread.py -j 8 /tmp/ompt-jacobi/ 32 > per-region.csv

### Eclipse Trace Compass and Data-Driven Analysis
Eclipse Trace Compass can be used to view, analyze and visualize the PInsight traces. See below the screen shot for LULESH tracing and visualization with Tracecompass.
The visualization was created in Trace Compass using [Data driven analysis](
 http://archive.eclipse.org/tracecompass/doc/stable/org.eclipse.tracecompass.doc.user/Data-driven-analysis.html#Data_driven_analysis). To generate visualizations like this yourself, look at the `tracecompass/` folder in this repository.

 ![Lulesh tracing and visualization with Tracecompass](doc/OMPT_LTTng_TraceCompass.png). 

-------------------------------------

### Analyses considered

 1. Overhead analysis.
 1. Load balancing analysis.
 1. Offline analysis for configuring power usage and frequency (perhaps binary-based?).

### For enabling callstack based tracing and flamegraph
 1. https://archive.eclipse.org/tracecompass/doc/stable/org.eclipse.tracecompass.doc.user/LTTng-UST-Analyses.html


