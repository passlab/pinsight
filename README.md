# Event Tracing and Visualization for OpenMP, MPI and CUDA programs

## PInsight shared library

The core of this project is a shared library called `pinsight`, which generates [LTTng][lttng] trace data for OpenMP [OMPT][ompt] events, MPI [PMPI events][pmpi], and CUDA [CUPTI events][cupti]. 

   [lttng]: https://lttng.org
   [ompt]: https://www.openmp.org/wp-content/uploads/ompt-tr.pdf
   [pmpi]: https://www.open-mpi.org/faq/?category=perftools#PMPI
   [cupti]: https://docs.nvidia.com/cuda/cupti/index.html

### Install for OpenMP OMPT event tracing

To be able to build/run the OMPT event tracing library, you will need to install several dependencies.

For Ubuntu 18.04/16.04 systems:

    sudo apt-get install build-essential cmake git

LTTng has some special [setup instructions][lttng-install].

   [lttng-install]: https://lttng.org/docs/v2.10/#doc-installing-lttng

    # After following the LTTng setup instructions:
    sudo apt-get install lttng-tools lttng-modules-dkms liblttng-ust-dev babeltrace

You will also need to have the [LLVM OpenMP runtime][llvm-openmp] installed and OMPT should be enabled to support runtime tracing. LLVM has combined modules into 
one github repo. You will need to clone the whole repo. For installing OpenMP library, llvm should allow to install the runtime instead of the whole LLVM. You can use an already-installed Clang/LLVM that have the OMPT-enabled OpenMP runtime and the required header files (omp.h and ompt.h). 

An example of that build process (taken from our `Dockerfile`), building and installing to `/home/yanyh/tools/llvm-openmp-install`:

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


### Build

To build the main `pinsight` shared library, use the cmake and make utilities. A path that includes omp.h and ompt.h headers needs to provided to cmake via `OPENMP_INCLUDE_PATH` setting. 

    git clone https://github.com/passlab/pinsight.git
    cd pinsight && mkdir build && cd build
    cmake -DOPENMP_INCLUDE_PATH=/home/yanyh/tools/llvm-openmp-install/include ..
    make

The instructions above will result in `build/libpinsight.so` to be created. 


### Run

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


For tracing MPI or MPI+OpenMP applications, e.g. trace LULESH

     scripts/trace.sh traces/LULESH-MPI-8npX4th LULESH-MPI-8npX4th ./lib/libpinsight.so /home/yanyh/tools/llvm-openmp-install:/opt/openmpi-install/lib mpirun -np 8 ./test/LULESH/build/lulesh2.0 

#### Specifying tracing rate
To allow user's control of tracing of each parallel region, one can specify a sampling rate, max number of traces, and initial number of traces of each parallel region using ``PINSIGHT_TRACE_CONFIG`` environment variable. The ``PINSIGHT_TRACE_CONFIG`` should be the form of ``<num_initial_traces>:<max_num_traces>:<trace_sampling_rate>``. Below are the examples of ``PINSIGHT_TRACE_CONFIG`` settings and their tracing behavior:
1. ``PINSIGHT_TRACE_CONFIG=10:50:10``, This is the system default. It records the first 10 traces, then after that, records one trace per 10 executions and in total max 50 traces will be recorded. 
1. ``PINSIGHT_TRACE_CONFIG=0:-1:-1``,  record all the traces. 
1. ``PINSIGHT_TRACE_CONFIG=0:-1:10``, record 1 trace per 10 executions for all the executions. 
1. ``PINSIGHT_TRACE_CONFIG=20:20:-1``, record the first 20 executions only


#### Dump the trace data to text using babeltrace
After tracing complete, you can use babeltrace tools to dump the trace data to text on the terminal

     babeltrace <tracefolder>
   

------------------------------------

#### Manual LTTng tracing session

Since we're using LTTng, we have to set up a [tracing session][lttng-tracing-session], and enable some [event rules][lttng-event-rules] before starting tracing:

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

In order to generate traces about OMPT events, our `libpinsight.so` library needs to be preloaded by the application.
We also need to specify which OpenMP runtime to use.

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

### Analysis

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


-----

### Screenshot

#### Lulesh tracing and visualization with Tracecompass
 ![Lulesh tracing and visualization with Tracecompass](doc/OMPT_LTTng_TraceCompass.png). The picture was created in Tracecompass using [Data driven analysis](
 http://archive.eclipse.org/tracecompass/doc/stable/org.eclipse.tracecompass.doc.user/Data-driven-analysis.html#Data_driven_analysis). To generate visualizations like this yourself, look at the `tracecompass/` folder in this repository.


### Analyses considered

 1. Overhead analysis.
 1. Load balancing analysis.
 1. Offline analysis for configuring power usage and frequency (perhaps binary-based?).

### For enabling callstack based tracing and flamegraph
 1. https://archive.eclipse.org/tracecompass/doc/stable/org.eclipse.tracecompass.doc.user/LTTng-UST-Analyses.html


#### Docker install

Docker makes it easy to build the project and its dependencies, and incurs only minimal overheads.
The commands below show how to create the image, and then get a container up and running.

    sudo docker build -f Dockerfile -t passlab-pinsight
    sudo docker create --name AAA --privileged passlab-pinsight

The `docker build` command creates an *image* called `passlab-pinsight`.
The `docker create` command creates a *container* named `AAA`, based on the `passlab-pinsight` image.

We need a privileged container to ensure that access to the underlying hardware isn't an issue.
(If this requirement can be dropped at a later time, we will drop it.)

To get a terminal open on the example container (for experimentation), try:

    sudo docker exect -it AAA /bin/bash
