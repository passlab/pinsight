# PInsight: In-Situ Performance Analysis for HPC Applications

PInsight is a lightweight, dynamic tracing and in-situ performance analysis framework for parallel applications using **OpenMP**, **MPI**, and **CUDA**. It intercepts runtime events via standard APIs (OMPT, PMPI, CUPTI), redirects them to [LTTng UST][lttng] for high-performance asynchronous trace collection, and provides a closed-loop **introspection** mechanism that lets applications analyze their own performance and adapt at runtime — without stopping the program.

   [lttng]: https://lttng.org
   [ompt]: https://www.openmp.org/wp-content/uploads/ompt-tr.pdf
   [pmpi]: https://www.open-mpi.org/faq/?category=perftools#PMPI
   [cupti]: https://docs.nvidia.com/cuda/cupti/index.html

## Key Features

- **Multi-domain tracing** — Unified tracing of OpenMP ([OMPT][ompt]), MPI ([PMPI][pmpi]), and CUDA ([CUPTI][cupti]) events on a single LTTng timeline
- **Asynchronous trace collection** — LTTng UST ring buffers decouple trace emission from disk I/O; near-zero overhead when no session is active
- **4-mode trace hierarchy** — OFF → STANDBY → MONITORING → TRACING, each adding exactly one layer of cost
- **Rate-limited tracing** — Per-region sampling (trace N-of-M executions) to reduce redundant data by orders of magnitude
- **Automatic mode switching** — Transition domains to lower-overhead modes after sufficient traces are collected
- **In-situ introspection (INTROSPECT)** — Automatically rotate traces, launch an analysis script, pause execution, and resume with optimized configuration — all without human intervention
- **Cyclic introspection** — Repeat the trace→analyze→tune cycle throughout a long-running application via generation-based counter reset
- **Runtime reconfiguration** — Edit the config file and send `SIGUSR1` to hot-reload tracing parameters, domain modes, and event filters

## Paper

The design and evaluation of PInsight is described in:

> **PInsight: In-Situ Performance Analysis for Adaptive Tuning of Parallel Applications**
> Yonghong Yan, et al. ACM/IEEE International Conference for High Performance Computing, Networking, Storage, and Analysis (SC'26), submitted.

### Contributions

1. **Asynchronous, low-overhead tracing**: Multi-domain tracing via OMPT/PMPI/CUPTI with LTTng UST backend, achieving <1% overhead for rate-limited tracing on production workloads (LULESH, Castro).

2. **Dynamic, hierarchical tracing control**: A 4-mode trace hierarchy (OFF/STANDBY/MONITORING/TRACING) with per-domain, per-event, per-punit, and per-lexgion configuration — enabling precise, adaptive control of tracing overhead at runtime.

3. **In-situ performance introspection**: A closed-loop mechanism where the application automatically pauses, rotates trace data, invokes an analysis script, and resumes with tuned parameters — enabling **self-adaptive performance optimization** without human in the loop.

---

## Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                   HPC Application                               │
│     OpenMP regions    MPI calls    CUDA kernels                 │
├────────┬──────────────┬────────────┬────────────────────────────┤
│  OMPT  │    PMPI      │   CUPTI    │  Callback / Wrapper APIs   │
│callbacks│  wrappers    │subscribers │                            │
├────────┴──────────────┴────────────┴────────────────────────────┤
│  PInsight Library (libpinsight.so)                              │
│  ┌────────────────────────────────────────────────────────────┐ │
│  │ Lexgion directory  │ Rate control │ 4-mode domain config  │ │
│  │ (per-thread LRU)   │ (per-region) │ (OFF/STANDBY/MON/TRC) │ │
│  ├────────────────────┴──────────────┴───────────────────────┤ │
│  │ Control Thread                                            │ │
│  │ • SIGUSR1 config reload  • INTROSPECT (pause/script/tune) │ │
│  │ • Automatic mode switch  • Cyclic generation counter      │ │
│  └───────────────────────────────────────────────────────────┘ │
├──────────────────────────────────────────────────────────────────┤
│  LTTng UST → per-CPU ring buffers → CTF trace files             │
└──────────────────────────────────────────────────────────────────┘
```

---

## Build

### Prerequisites

```bash
# Ubuntu/Debian
sudo apt-get install build-essential cmake git
sudo apt-get install lttng-tools lttng-modules-dkms liblttng-ust-dev babeltrace2
```

- **OpenMP**: Clang/LLVM with OMPT support (provides `omp.h`, `omp-tools.h`, `libomp.so`)
- **CUDA**: NVIDIA CUDA SDK with CUPTI (default: `/usr/local/cuda`)
- **MPI**: Any MPI implementation supporting PMPI (OpenMPI, MPICH, etc.)

### Build the PInsight Library

```bash
git clone https://github.com/passlab/pinsight.git
cd pinsight && mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make -j$(nproc)
```

This produces `build/libpinsight.so`.

### Build Options

| Option | Default | Description |
|--------|---------|-------------|
| `PINSIGHT_OPENMP` | TRUE | OpenMP OMPT event tracing |
| `PINSIGHT_MPI` | FALSE | MPI PMPI event tracing |
| `PINSIGHT_CUDA` | TRUE | CUDA CUPTI event tracing |
| `PINSIGHT_ENERGY` | FALSE | Intel RAPL energy monitoring |
| `PINSIGHT_BACKTRACE` | TRUE | Stack backtrace in trace records |

Pass options to cmake, e.g.:
```bash
cmake -DPINSIGHT_MPI=TRUE -DPINSIGHT_CUDA=TRUE -DCUDA_INSTALL=/usr/local/cuda ..
```

For OpenMP tracing, the PInsight `src/` folder contains copies of `omp.h` and `omp-tools.h`
from Clang/LLVM. To use headers from a different location:
```bash
cmake -DOPENMP_INCLUDE_PATH=/path/to/llvm-install/include ..
```

---

## Usage

### Quick Start with `trace.sh`

The `scripts/trace.sh` script automates LTTng session setup:

```bash
Usage: trace.sh TRACEFILE_DEST TRACE_NAME PINSIGHT_LIB LD_LIBRARY_PATH_PREPEND PROG_AND_ARGS...
```

**Examples:**

```bash
# Trace an OpenMP application
OMP_NUM_THREADS=8 bash scripts/trace.sh \
    ./traces/jacobi jacobi \
    ./build/libpinsight.so : \
    ./test/jacobi/jacobi 512

# Trace an MPI+OpenMP application
OMP_NUM_THREADS=4 bash scripts/trace.sh \
    ./traces/lulesh LULESH \
    ./build/libpinsight.so : \
    mpirun -np 8 ./test/LULESH/build/lulesh2.0 -s 20

# Trace an MPI+CUDA application
bash scripts/trace.sh \
    ./traces/castro Castro \
    ./build/libpinsight.so : \
    mpirun -np 4 ./eva/Castro/Exec/hydro_tests/Sedov/Castro3d.gnu.MPI.CUDA.ex inputs.3d.e2eval
```

### Manual LTTng Session

```bash
# 1. Create session
lttng create my-session --output=./my-traces

# 2. Enable PInsight events
lttng enable-event --userspace 'ompt_pinsight_lttng_ust:*'
lttng enable-event --userspace 'pmpi_pinsight_lttng_ust:*'
lttng enable-event --userspace 'cupti_pinsight_lttng_ust:*'

# 3. Start tracing
lttng start

# 4. Run application with PInsight
LD_PRELOAD=./build/libpinsight.so ./your_application

# 5. Stop and collect
lttng stop && lttng destroy
```

### View Traces

```bash
# Text dump
babeltrace2 ./my-traces

# Event count
babeltrace2 ./my-traces | wc -l
```

---

## Runtime Configuration

### Domain Trace Modes

Each domain can operate in one of four modes:

| Mode | Callbacks | Lexgion tracking | LTTng output | Overhead | Reversible |
|------|-----------|-----------------|-------------|----------|------------|
| **OFF** | Deregistered | ❌ | ❌ | Zero | No (permanent) |
| **STANDBY** | Immediate return | ❌ | ❌ | ~2ns | Yes |
| **MONITORING** | Active | ✅ Count + LRU | ❌ | ~20ns | Yes |
| **TRACING** | Active | ✅ Full | ✅ | ~50-200ns | Yes |

### Environment Variables

```bash
# Domain mode control
PINSIGHT_TRACE_OPENMP=<MODE>     # OFF | STANDBY | MONITORING | TRACING (default: TRACING)
PINSIGHT_TRACE_MPI=<MODE>
PINSIGHT_TRACE_CUDA=<MODE>

# Rate-based sampling: trace_starts_at:max_num_traces:tracing_rate[:mode_after]
PINSIGHT_TRACE_RATE=0:100:1:MONITORING

# Config file path
PINSIGHT_TRACE_CONFIG_FILE=/path/to/config.txt

# Debug output
PINSIGHT_DEBUG_ENABLE=0|1
```

**Rate-based sampling examples:**

| Setting | Meaning |
|---------|---------|
| `0:-1:1` | Record all traces (default) |
| `0:100:1` | Record first 100 executions per region |
| `10:20:100` | Skip first 10, then 1-in-100 for 20 traces |
| `0:100:1:MONITORING` | First 100 traces, then switch to MONITORING |
| `0:50:1:INTROSPECT:10:analyze.sh:TRACING` | First 50 traces, then introspect and resume |

### Config File

PInsight supports an INI-style config file for fine-grained, per-domain and per-region control.
The file is searched in this order:

1. `PINSIGHT_TRACE_CONFIG_FILE` environment variable
2. `pinsight_trace_config.txt` in the current working directory

```ini
# Example: rate-limited tracing with automatic mode switch
[OpenMP]
    trace_mode = TRACING

[OpenMP.punit.thread]
    range = 0-7

[Lexgion.default]
    max_num_traces = 100
    tracing_rate = 1
    trace_mode_after = MONITORING
```

See [`doc/PINSIGHT_TRACE_CONFIG_FORMAT.md`](doc/PINSIGHT_TRACE_CONFIG_FORMAT.md) for the full config format specification.

### Runtime Reconfiguration via SIGUSR1

Domain modes and tracing options can be changed at runtime without restarting:

1. Edit the config file
2. Send `kill -USR1 <pid>` to the running application
3. PInsight re-reads the config and re-registers/deregisters callbacks dynamically

---

## In-Situ Introspection (INTROSPECT)

PInsight's most distinctive feature is the **INTROSPECT** mechanism — a closed-loop workflow where the application can analyze its own performance and adapt at runtime.

### How It Works

```
Application running (TRACING mode)
  │
  ├─ Lexgion reaches max_num_traces ──→ Control thread triggered
  │                                      │
  │                                      ├─ lttng rotate (flush traces)
  │                                      ├─ posix_spawn(analysis_script)
  │                                      ├─ Application PAUSED
  │                                      │
  │                                      │  Script analyzes traces,
  │                                      │  writes new config,
  │                                      │  sends SIGUSR1 to resume
  │                                      │
  │                                      ├─ Config reloaded
  │                                      ├─ Application RESUMED
  │                                      └─ Domain modes applied
  │
  └─ If resume_mode = TRACING: cycle repeats (cyclic introspection)
```

### Configuration

```ini
[Lexgion.default]
    max_num_traces = 100
    trace_mode_after = INTROSPECT:10:analyze.sh:TRACING
    #                  ^^^^^^^^^^  ^^  ^^^^^^^^^^  ^^^^^^^
    #                  action    timeout  script  resume_mode
```

| Field | Description |
|-------|-------------|
| `INTROSPECT` | Action keyword |
| Timeout | Seconds to wait (0 = no pause, -1 = wait indefinitely for SIGUSR1) |
| Script | Analysis script path (`-` = none) |
| Resume mode | `TRACING` (cyclic), `MONITORING`, `STANDBY`, or `OFF` |

### Cyclic Introspection

When `resume_mode = TRACING`, the introspection cycle repeats automatically. PInsight uses
a **generation counter** to ensure all lexgions (across all threads) reset their trace
counters at the start of each new cycle, producing evenly spaced analysis windows:

```
Cycle 1: Trace 100 events → INTROSPECT → analyze → resume
Cycle 2: Trace 100 events → INTROSPECT → analyze → resume
Cycle 3: ...
```

---

## Multi-Domain Tracing

### OpenMP Events (via OMPT)
- Thread begin/end, parallel begin/end, implicit task begin/end
- Work constructs (`omp for`, `sections`, `single`, `distribute`), masked/master
- Synchronization: barriers, taskwait, taskgroup, sync_region_wait
- Tasks: task_create, task_schedule
- Target offload, device init/finalize/load/unload

### MPI Events (via PMPI)
- `MPI_Init`, `MPI_Init_thread`, `MPI_Finalize`
- `MPI_Send`, `MPI_Recv`, `MPI_Barrier`, `MPI_Reduce`, `MPI_Allreduce`

### CUDA Events (via CUPTI)
- Kernel launch/complete/enqueue
- Memory: malloc, free, memcpy (HtoD, DtoH, DtoD), memset
- Streams, events, context, device, synchronization

---

## Analysis and Visualization

| Tool | Purpose |
|------|---------|
| **babeltrace2** | CLI text dump and filtering of CTF traces |
| **[Trace Compass](https://eclipse.dev/tracecompass/)** | Eclipse-based GUI for trace analysis — timeline, statistics, call graph |
| **[PEAM](https://github.com/passlab/peam)** | Python-based analysis scripts for PInsight traces |
| **trace.sh** | Helper script for automated LTTng session management |

---

## Documentation

| Document | Description |
|----------|-------------|
| [`doc/PINSIGHT_TRACE_CONFIG_FORMAT.md`](doc/PINSIGHT_TRACE_CONFIG_FORMAT.md) | Config file format specification |
| [`doc/four_mode_trace_design.md`](doc/four_mode_trace_design.md) | 4-mode trace hierarchy design |
| [`doc/control_thread_design.md`](doc/control_thread_design.md) | Control thread and INTROSPECT architecture |
| [`doc/domain_trace_modes.md`](doc/domain_trace_modes.md) | Domain mode benchmark results |
| [`doc/rate-limit-tracing.md`](doc/rate-limit-tracing.md) | Rate-based sampling design |
| [`doc/python_tracing_design.md`](doc/python_tracing_design.md) | Python domain support design (planned) |
| [`doc/cuda_support_design.md`](doc/cuda_support_design.md) | CUDA/CUPTI tracing design |

---

## License

See [LICENSE](LICENSE) for details.
