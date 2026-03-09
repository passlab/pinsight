# PInsight: Dynamic and Asynchronous Tracing for OpenMP, MPI and CUDA Programs

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
from Clang/LLVM 14.0. If you need to use the two headers from other locations,  pass to cmake via `OPENMP_INCLUDE_PATH` setting. 
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
    trace.sh PINSIGHT_LIB LD_LIBRARY_PATH_PREPEND TRACE_NAME TRACEFILE_DEST PROG_AND_ARGS...

Arguments:
  PINSIGHT_LIB      Full-path PInsight shared library file name to use with user application.
  LD_LIBRARY_PATH_PREPEND   A list of paths separated by :. The list is prepended to 
                    the LD_LIBRARY_PATH env. This argument can be used to provide
                    path for the libraries used by the pinsight tracing, such as
                    path for libomp.so or libmpi.so. If none is needed, e.g. the path are
                    already set in the LD_LIBRARY_PATH, : should be provided for this arg.
  TRACE_NAME        Give a proper name for the trace to be displayed in tracecompass.
  TRACEFILE_DEST    Where to write the LTTng traces.
  PROG_AND_ARGS...  The program and its argument to be traced

Examples:
    trace.sh /opt/pinsight/lib/libpinsight.so /opt/llvm-install/lib jacobi ./traces/jacobi ./test/jacobi 2048 2048
    
    trace.sh /opt/pinsight/lib/libpinsight.so /opt/llvm-install/lib:/opt/openmpi-install/lib LULESH ./traces/LULESH mpirun -np 8 test/LULESH/build/lulesh2.0 -s 20
```

Example using the `jacobi` application with `8` threads:

```
yyan7@yyan7-Ubuntu:~/tools/pinsight$ export OMP_NUM_THREADS=8
yyan7@yyan7-Ubuntu:~/tools/pinsight$ ./scripts/trace.sh ./build/libpinsight.so /home/yyan7/compiler/llvm-openmp-install/lib jacobi ./traces/jacobi test/jacobi/jacobi 512
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

     scripts/trace.sh ./lib/libpinsight.so /home/yanyh/tools/llvm-openmp-install:/opt/openmpi-install/lib \
     LULESH-MPI-8npX4th traces/LULESH-MPI-8npX4th mpirun -np 8 ./test/LULESH/build/lulesh2.0

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


## Runtime Configuration for Optimizing Dynamic Tracing

PInsight supports fine-grained control of tracing overhead through three domain trace modes and flexible runtime configuration.

### Domain Trace Modes

Each domain (OpenMP, MPI, CUDA) can operate in one of three modes:

| Mode | Description | Overhead |
|------|-------------|----------|
| **OFF** | Domain callbacks deregistered; zero per-event overhead | None |
| **MONITORING** | Bookkeeping active (lexgion creation, counters); no LTTng output | Low |
| **TRACING** | Full tracing with LTTng tracepoint emission (default) | Normal |

- **OFF mode** uses native deregistration APIs (`ompt_set_callback(event, NULL)` for OpenMP) so the runtime never dispatches the event — truly zero overhead.
- **MONITORING mode** maintains internal bookkeeping (lexgion tables, execution counters, rate sampling) without emitting traces, enabling later analysis of program structure.
- **TRACING mode** emits full LTTng tracepoints. Overhead depends on whether an LTTng session is active.

For detailed benchmark results, see [`doc/domain_trace_modes.md`](doc/domain_trace_modes.md).

### Environment Variables

#### Domain Mode Control

```bash
PINSIGHT_TRACE_OPENMP=<MODE>     # OFF | MONITORING | TRACING (default: TRACING)
PINSIGHT_TRACE_MPI=<MODE>        # OFF | MONITORING | TRACING (default: TRACING)
PINSIGHT_TRACE_CUDA=<MODE>       # OFF | MONITORING | TRACING (default: TRACING)
```

Accepted values for each mode:
| Mode | Accepted Values |
|------|----------------|
| OFF | `OFF`, `FALSE`, `0` |
| MONITORING | `MONITORING`, `MONITOR` |
| TRACING | `ON`, `TRACING`, `TRUE`, `1` (default) |

Examples:
```bash
# Full tracing (default behavior)
./myapp

# Monitor OpenMP regions without trace output
PINSIGHT_TRACE_OPENMP=MONITORING ./myapp

# Completely disable OpenMP tracing (zero overhead)
PINSIGHT_TRACE_OPENMP=OFF ./myapp

# Mix: trace MPI, monitor OpenMP, disable CUDA
PINSIGHT_TRACE_MPI=TRUE PINSIGHT_TRACE_OPENMP=MONITORING PINSIGHT_TRACE_CUDA=OFF ./myapp
```

#### Rate-Based Sampling

```bash
PINSIGHT_TRACE_RATE=<trace_starts_at>:<max_num_traces>:<tracing_rate>
```

Controls how frequently lexgion executions are traced:

| Example | Meaning |
|---------|---------|
| `0:-1:1` | Record all traces (system default) |
| `10:20:100` | Record 20 traces starting from 10th iteration, 1 per 100 |
| `0:-1:10` | Record 1 trace per 10 executions, indefinitely |
| `0:20:1` | Record the first 20 executions |
| `0:0:0` | Do not record any traces |

#### Other Environment Variables

```bash
PINSIGHT_TRACE_ENERGY=TRUE|FALSE       # Energy tracing via RAPL
PINSIGHT_TRACE_BACKTRACE=TRUE|FALSE    # Backtrace capture
PINSIGHT_DEBUG_ENABLE=0|1              # Debug printouts (default: 0)
```

### Config File

A config file provides domain-specific and lexgion-specific configuration. Specify the file via:

```bash
export PINSIGHT_TRACE_CONFIG_FILE=/path/to/trace_config.txt
```

The config file supports two domain-level section types:

- **`[Domain.global]`** — Domain-wide structural settings: `trace_mode` and punit scope
- **`[Domain.default]`** — Default event on/off configuration for lexgions

Example:
```ini
[OpenMP.global]
    trace_mode = OFF             # OFF | MONITORING | TRACING
    OpenMP.thread = (0-7)

[OpenMP.default]
    omp_task_create = off
```

See [`doc/trace_config_example.txt`](doc/trace_config_example.txt) for the full format, and [`doc/PINSIGHT_TRACE_CONFIG_FORMAT.md`](doc/PINSIGHT_TRACE_CONFIG_FORMAT.md) for detailed documentation.

### Runtime Reconfiguration via SIGUSR1

Domain modes and tracing options can be changed at runtime by editing the config file and signaling the process:

1. Edit the config file (e.g., change `trace_mode = TRACING` to `trace_mode = OFF`)
2. Send `kill -USR1 <pid>` to the running application
3. PInsight re-reads the config and re-registers/deregisters domain callbacks accordingly

> **Note:** Environment variables are read at process launch and cannot be changed from outside a running process. For runtime reconfiguration, use the config file.

This enables workflows such as:
- Start with **OFF** mode for warm-up, switch to **TRACING** for the region of interest
- Start with **TRACING** to capture initial behavior, switch to **MONITORING** to reduce overhead
- Runtime performance debugging: pause tracing, analyze, reconfigure, resume

---------------------------------------------------------------------------

## View, Analysis, and Visualize PInsight Traces

### Trace text dump
After tracing complete, you can use babeltrace tool to dump the trace data to text on the terminal

     babeltrace <tracefolder>

### Analysis using Python script and Trace Compasss, please check [Performance and Execution Analysis and Monitoring (PEAM)](https://github.com/passlab/peam)



