# PInsight: In-Situ Performance Analysis for HPC Applications

PInsight is a lightweight, dynamic tracing and in-situ performance analysis framework for parallel applications using **OpenMP**, **MPI**, **CUDA**, **AMD HIP/ROCm**, and **Python**. It intercepts runtime events via standard APIs (OMPT, PMPI, CUPTI, ROCTracer, and Python's [`sys.monitoring`][sysmon]), redirects them to [LTTng UST][lttng] for high-performance asynchronous trace collection, and provides a closed-loop **introspection** mechanism that lets applications analyze their own performance and adapt at runtime — without stopping the program.

   [lttng]: https://lttng.org
   [ompt]: https://www.openmp.org/wp-content/uploads/ompt-tr.pdf
   [pmpi]: https://www.open-mpi.org/faq/?category=perftools#PMPI
   [cupti]: https://docs.nvidia.com/cuda/cupti/index.html
   [roctracer]: https://rocm.docs.amd.com/projects/roctracer/en/latest/
   [sysmon]: https://docs.python.org/3/library/sys.monitoring.html

## Key Features

- **Multi-domain tracing** — Unified tracing of OpenMP ([OMPT][ompt]), MPI ([PMPI][pmpi]), CUDA ([CUPTI][cupti]), AMD HIP/ROCm ([ROCTracer][roctracer]), and Python ([`sys.monitoring`][sysmon]) events on a single LTTng timeline
- **Asynchronous trace collection** — LTTng UST ring buffers decouple trace emission from disk I/O; near-zero overhead when no session is active
- **4-mode trace hierarchy** — OFF → STANDBY → MONITORING → TRACING, each adding exactly one layer of cost
- **Rate-limited tracing** — Per-region sampling (trace N-of-M executions) to reduce redundant data by orders of magnitude
- **Automatic mode switching** — Transition domains to lower-overhead modes after sufficient traces are collected
- **In-situ introspection (INTROSPECT)** — Automatically rotate traces, launch an analysis script, pause execution, and resume with optimized configuration — all without human intervention
- **Cyclic introspection** — Repeat the trace→analyze→tune cycle throughout a long-running application via generation-based counter reset
- **Runtime reconfiguration** — Edit the config file and send `SIGUSR1` to hot-reload tracing parameters, domain modes, and event filters

## Architecture

```
┌──────────────────────────────────────────────────────────────────────┐
│                       HPC / Python Application                          │
│   OpenMP regions   MPI calls   CUDA kernels   HIP kernels   Python     │
├──────────┬─────────┬───────────┬─────────────┬─────────────────────────┤
│   OMPT   │  PMPI   │   CUPTI   │  ROCTracer  │  sys.monitoring (PEP669) │
│ callbacks│ wrappers│subscribers│  callbacks  │  + Callback/Wrapper APIs │
├──────────┴─────────┴───────────┴─────────────┴─────────────────────────┤
│  PInsight Library (libpinsight.so)                                     │
│  ┌─────────────────────────────────────────────────────────────────┐  │
│  │ Lexgion directory  │ Rate control │ 4-mode domain config        │  │
│  │ (per-thread LRU)   │ (per-region) │ (OFF/STANDBY/MON/TRC)       │  │
│  ├────────────────────┴──────────────┴─────────────────────────────┤  │
│  │ Control Thread                                                  │  │
│  │ • SIGUSR1 config reload  • INTROSPECT (pause/script/tune)       │  │
│  │ • Automatic mode switch  • Cyclic generation counter            │  │
│  └─────────────────────────────────────────────────────────────────┘  │
├──────────────────────────────────────────────────────────────────────┤
│  LTTng UST → per-CPU ring buffers → CTF trace files                   │
└──────────────────────────────────────────────────────────────────────┘
```

---

## Build

### Prerequisites

```bash
# Ubuntu/Debian
sudo apt-get install build-essential cmake git
sudo apt-get install lttng-tools liblttng-ust-dev babeltrace2
```

- **OpenMP**: Clang/LLVM with OMPT support (provides `omp.h`, `omp-tools.h`, `libomp.so`)
- **CUDA**: NVIDIA CUDA SDK with CUPTI (default: `/usr/local/cuda`)
- **MPI**: Any MPI implementation supporting PMPI (OpenMPI, MPICH, etc.)
- **AMD HIP/ROCm**: ROCm with ROCTracer (`libroctracer64.so`, default: `/opt/rocm`); set `ROCM_PATH` for a non-default install
- **Python**: CPython 3.12+ (uses [`sys.monitoring`][sysmon], PEP 669)

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
| `PINSIGHT_HIP` | FALSE | AMD HIP/ROCm ROCTracer event tracing |
| `PINSIGHT_PYTHON` | FALSE | Python `sys.monitoring` event tracing (CPython 3.12+) |
| `PINSIGHT_ENERGY` | FALSE | Intel RAPL energy monitoring |
| `PINSIGHT_BACKTRACE` | TRUE | Stack backtrace in trace records |

Pass options to cmake, e.g.:
```bash
# CUDA (NVIDIA)
cmake -DPINSIGHT_MPI=TRUE -DPINSIGHT_CUDA=TRUE -DCUDA_INSTALL=/usr/local/cuda ..

# AMD HIP/ROCm
cmake -DPINSIGHT_CUDA=FALSE -DPINSIGHT_HIP=TRUE -DROCM_PATH=/opt/rocm ..

# Python
cmake -DPINSIGHT_PYTHON=TRUE ..
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
    mpirun -np 4 /path/to/Castro/Exec/hydro_tests/Sedov/Castro3d.gnu.MPI.CUDA.ex inputs.3d.e2eval
```

### Manual LTTng Session

```bash
# 1. Create session
lttng create my-session --output=./my-traces

# 2. Enable PInsight events (enable only the providers you built)
lttng enable-event --userspace 'ompt_pinsight_lttng_ust:*'       # OpenMP
lttng enable-event --userspace 'pmpi_pinsight_lttng_ust:*'       # MPI
lttng enable-event --userspace 'cupti_pinsight_lttng_ust:*'      # CUDA
lttng enable-event --userspace 'roctracer_pinsight_lttng_ust:*'  # AMD HIP/ROCm
lttng enable-event --userspace 'pysysmon_pinsight_lttng_ust:*'   # Python
lttng enable-event --userspace 'pinsight_enter_exit_lttng_ust:*' # process enter/exit
lttng enable-event --userspace 'pinsight_manifest_lttng_ust:*'   # trace manifest (provenance)

# 3. Start tracing
lttng start

# 4. Run application with PInsight
LD_PRELOAD=./build/libpinsight.so ./your_application

# 5. Stop and collect
lttng stop && lttng destroy
```

### Running under a Job Scheduler (Flux, Slurm)

Batch/multi-node launches change *where the LTTng session daemon lives and
how long it survives* — getting this wrong is the most common cause of
"job ran fine, trace is empty". The four invariants:

1. **One LTTng session per node** (`--output=$OUT/$(hostname)`); traces are grouped per node afterwards.
2. **`LTTNG_HOME` must be node-local** (e.g. `/tmp/$USER/lttng-$JOBID`) — NFS-shared `$HOME` makes concurrent nodes' daemons collide — and set for both the `lttng` CLI and the traced app.
3. **The sessiond must live in the same job step as the app** — a daemon started in its own `flux run`/`srun` invocation is killed with that step's cgroup before the app starts. Multi-node: fold session start → app → session stop into the single launched task each rank runs.
4. **Session setup is idempotent, not coordinated** — every local rank attempts `lttng create/start/stop/destroy`; the fastest wins, the rest fail harmlessly.

See [`doc/user/scheduler_launching.md`](doc/user/scheduler_launching.md) for
complete recipes (plain `mpirun`, Slurm single-node, Flux/Slurm multi-node),
GPU-binding caveats (`--gpus-per-task` devId collapse), manifest wiring, and
troubleshooting.

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
PINSIGHT_TRACE_HIP=<MODE>
PINSIGHT_TRACE_PYTHON=<MODE>

# Tracing window: start:max_num_traces:tracing_rate:window_timeout[:window_end_action]
# (window_timeout = wall-clock seconds to end the window, 0 = disabled)
PINSIGHT_TRACE_WINDOW=0:100:1:0:MONITORING
# PINSIGHT_TRACE_RATE is a deprecated alias (same grammar, prints a warning).

# Config file path
PINSIGHT_TRACE_CONFIG_FILE=/path/to/config.txt

# Trace manifest (normally exported by scripts/pinsight-manifest.sh)
PINSIGHT_RUN_ID=<id>              # run identifier; if unset, one is generated (and unified at MPI_Init)
PINSIGHT_MANIFEST_DIR=/path/dir   # where effective-config dumps are written; unset = no files written

# Debug output
PINSIGHT_DEBUG_ENABLE=0|1
```

**Tracing-window examples** (`start:max:rate:window_timeout[:window_end_action]`):

| Setting | Meaning |
|---------|---------|
| `0:-1:1:0` | Record all traces (default) |
| `0:100:1:0` | Record first 100 executions per region |
| `10:20:100:0` | Skip first 10, then 1-in-100 for 20 traces |
| `0:100:1:0:MONITORING` | First 100 traces, then switch to MONITORING |
| `0:-1:1:30:MONITORING` | Trace for 30 wall-clock seconds, then MONITORING |
| `0:50:1:60:INTROSPECT:10:analyze.sh:TRACING` | First 50 traces *or* 60 s, then introspect and resume |

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
    window_end_action = MONITORING
```

See [`doc/user/trace_config_format.md`](doc/user/trace_config_format.md) for the full config format specification.

### Trace Manifest (provenance)

PInsight traces can be made **self-describing**: with the manifest provider
enabled (`lttng enable-event -u 'pinsight_manifest_lttng_ust:*'`), every
process periodically emits its provenance — run id, rank, binding as seen,
binary build-id, effective-config hash — directly into the trace, and the
launcher-side [`scripts/pinsight-manifest.sh`](scripts/pinsight-manifest.sh)
assembles the run-level sidecar (`run_manifest.json`: job metadata, per-node
hardware inventory, user-provided facts, trace integrity hashes), with or
without a job scheduler.

The periodic burst cadence is set in the config file (SIGUSR1-reloadable);
there is deliberately no on/off key — enabling the LTTng provider is the switch:

```ini
[Manifest]
    interval = 10    # seconds between periodic bursts; 0 = startup/transition bursts only (default: 10)
```

See the user guide: [`doc/user/manifest.md`](doc/user/manifest.md).

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
    window_end_action = INTROSPECT:10:analyze.sh:TRACING
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

### AMD HIP/ROCm Events (via ROCTracer)
- Kernel launch (Callback API) + actual GPU execution timing (Activity API)
- Memory: memcpy (HtoD, DtoH, DtoD, HtoH) sync/async + GPU-side transfer timing
- Device/stream synchronization, device reset
- Callback and Activity records linked by `correlation_id`; CPU↔GPU clocks aligned via a one-shot calibration anchor

### Python Events (via `sys.monitoring`, PEP 669)
- Function call/return (PyFunction), named-lexgion filtering by qualified name
- Per-thread tracing with rate control, integrating into the same lexgion/mode hierarchy

### Manifest Events (provenance, provider `pinsight_manifest_lttng_ust`)
- `manifest_process` — per-process provenance burst: sequence number, reason (`init`, `mpi_init`, periodic, window/config transitions), MPI rank, executable path, window generation
- `manifest_kv` — generic `(key, value)` facts (run id, binding as seen, binary build-id, effective-config hash, launcher/GPU/host facts); new facts need no schema change

---

## Analysis and Visualization

Trace-analysis artifacts live in the [`analysis/`](analysis/) folder — a Python toolkit for
PInsight CTF traces built on a common reader (`analysis/pinsight_reader.py`, requires
`babeltrace2`). It includes app-agnostic analysis scripts (load imbalance, MPI latency,
GPU data movement, halo exchange, MPI/GPU/energy reports) and all TraceCompass GUI
integration under [`analysis/tc/`](analysis/tc/) (LAMI external analyses + XML data-driven
analyses, with a one-shot `tc-setup.sh` installer). See [`analysis/README.md`](analysis/README.md)
for the full script list and usage.

| Tool | Purpose |
|------|---------|
| **[`analysis/`](analysis/)** | Python analysis scripts for PInsight traces (load imbalance, MPI latency, GPU data movement, halo exchange, energy) |
| **[`analysis/tc/`](analysis/tc/)** | TraceCompass integration: LAMI external analyses and XML analyses |
| **babeltrace2** | CLI text dump and filtering of CTF traces |
| **[Trace Compass](https://eclipse.dev/tracecompass/)** | Eclipse-based GUI for trace analysis — timeline, statistics, call graph |
| **trace.sh** | Helper script for automated LTTng session management |

---

## Documentation

User documentation lives in [`doc/user/`](doc/user/) — the PInsight user
guide; see [`doc/README.md`](doc/README.md) for the index.

| Document | Description |
|----------|-------------|
| [`doc/user/trace_config_format.md`](doc/user/trace_config_format.md) | Config file format specification |
| [`doc/user/manifest.md`](doc/user/manifest.md) | Trace manifest: self-describing traces + run sidecar (`pinsight-manifest.sh`) |
| [`doc/user/scheduler_launching.md`](doc/user/scheduler_launching.md) | Running under Flux/Slurm: per-node sessions, multi-node recipes, GPU binding |
| [`doc/user/domain_trace_modes.md`](doc/user/domain_trace_modes.md) | Domain trace modes and benchmark results |
| [`doc/user/rate-limit-tracing.md`](doc/user/rate-limit-tracing.md) | Rate-controlled tracing |
| [`doc/user/python_trace_config.md`](doc/user/python_trace_config.md) | Python tracing configuration reference |

---

## License

See [LICENSE](LICENSE) for details.
