# OpenMP OMPT Event Tracing and Visualization

## PInsight shared library

The core of this project is a shared library called `pinsight`, which generates [LTTng][lttng] trace data for OpenMP [OMPT][ompt] events.

   [lttng]: https://lttng.org
   [ompt]: https://www.openmp.org/wp-content/uploads/ompt-tr.pdf

### Install

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


#### Manual install

To be able to build/run the OMPT event tracing library, you will need to install several dependencies.

For Ubuntu 16.04 systems:

    sudo apt-get install build-essential cmake git

LTTng has some special [setup instructions][lttng-install].

   [lttng-install]: https://lttng.org/docs/v2.10/#doc-installing-lttng

    # After following the LTTng setup instructions:
    sudo apt-get install lttng-tools lttng-modules-dkms liblttng-ust-dev

You will also need to build the [LLVM OpenMP runtime][llvm-openmp].

An example of that build process (taken from our `Dockerfile`), building and installing to `/home/yanyh/tools/llvm-openmp-install`:

    git clone https://github.com/llvm-mirror/openmp.git && \
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

   [llvm-openmp]: https://github.com/llvm-mirror/openmp


### Build

To build the main `pinsight` shared library, use the cmake and make utilities. OpenMP library path needs to be provided to
cmake as the value for the OMPLIB_INSTALL variable. 
    git clone https://github.com/passlab/pinsight.git
    mkdir build && cd build
    cmake -DOMPLIB_INSTALL=/home/yanyh/tools/llvm-openmp-install ..
    make

The instructions above will result in `build/libpinsight.so` being located at `build/libpinsight.so`.


### Run

#### Tracing script

In the `scripts/` directory, a script called `trace.sh` is provided.
This script helps make generating LTTng traces for MPI/OpenMP/CUDA programs easier.

Example using the `jacobi` application with `8` threads:

    trace.sh traces/jacobi jacobi \
      /home/yanyh/tools/llvm-openmp-install/lib/libomp.so \
      build/libpinsight.so \
      8 \
      ./jacobi 2048 2048

For jacobi on my vm:
./scripts/trace.sh traces/jacobi jacobi  /home/yanyh/tools/llvm-openmp-install/lib/libomp.so   build/libpinsight.so 8 ./test/jacobi/jacobi 2048 2048

For tracing MPI or MPI+OpenMP applications, e.g. trace LULESH

     scripts/trace.sh traces/LULESH-MPI-8npX4th LULESH-MPI-8npX4th /home/yanyh/tools/llvm-openmp-install/lib/libomp.so ./lib/libpinsight.so 4 mpirun -np 8 ./test/LULESH/build/lulesh2.0 

#### Specifying tracing rate
To allow user's control of tracing of each parallel region, one can specify a sampling rate, max number of traces, and initial number of traces of each parallel region using ``PINSIGHT_TRACE_CONFIG`` environment variable. The ``PINSIGHT_TRACE_CONFIG`` should be the form of ``<num_initial_traces>:<max_num_traces>:<trace_sampling_rate>``. Below are the examples of ``PINSIGHT_TRACE_CONFIG`` settings and their tracing behavior:
1. ``PINSIGHT_TRACE_CONFIG=10:50:10``, This is the system default. It records the first 10 traces, then after that, records one trace per 10 executions and in total max 50 traces will be recorded. 
1. ``PINSIGHT_TRACE_CONFIG=0:-1:-1``,  record all the traces. 
1. ``PINSIGHT_TRACE_CONFIG=0:-1:10``, record 1 trace per 10 executions for all the executions. 
1. ``PINSIGHT_TRACE_CONFIG=20:20:-1``, record the first 20 executions only

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
