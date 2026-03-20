# PInsight: Feature and Capability Summary

PInsight is a dynamic, asynchronous tracing library for parallel applications using OpenMP, MPI, and CUDA. It intercepts runtime events via standard APIs, redirects them to [LTTng UST](https://lttng.org) for high-performance asynchronous trace collection, and provides flexible runtime configuration to minimize overhead.

---

## 1. Multi-Domain Tracing

PInsight traces three parallel programming domains via native interposition APIs:

| Domain | Interposition API | Mechanism | Key Source |
|--------|------------------|-----------|------------|
| **OpenMP** | [OMPT](https://www.openmp.org/wp-content/uploads/ompt-tr.pdf) | `ompt_set_callback()` — 35+ event callbacks | [ompt_callback.c](file:///home/yyan7/work/tools/pinsight/src/ompt_callback.c) (1733 lines) |
| **MPI** | [PMPI](https://www.open-mpi.org/faq/?category=perftools#PMPI) | Link-time wrapper interposition | [pmpi_mpi.c](file:///home/yyan7/work/tools/pinsight/src/pmpi_mpi.c) (8 MPI functions) |
| **CUDA** | [CUPTI](https://docs.nvidia.com/cuda/cupti/) | `cuptiSubscribe()` + callback | [cupti_callback.c](file:///home/yyan7/work/tools/pinsight/src/cupti_callback.c) |

### OpenMP Events Traced (via OMPT)
- Thread begin/end, parallel begin/end, implicit task begin/end
- Work constructs (`omp for`, `sections`, `single`, `distribute`), masked/master
- Synchronization: barriers, taskwait, taskgroup, sync_region_wait
- Tasks: task_create, task_schedule
- Target offload, device init/finalize/load/unload
- Mutex, lock, flush, cancel, reduction, dispatch

### MPI Functions Traced (via PMPI)
- `MPI_Init`, `MPI_Init_thread`, `MPI_Finalize`
- `MPI_Send`, `MPI_Recv`, `MPI_Barrier`, `MPI_Reduce`, `MPI_Allreduce`

### CUDA Operations Traced (via CUPTI)
- Kernel launch/complete/enqueue
- Memory: malloc, free, memcpy (HtoD, DtoH, DtoD, HtoH), memset, managed memory
- Streams, events, context, device, synchronization
- Graphs, modules, unified memory, NVTX markers

### Planned: HIP Support
An [HIP event config template](file:///home/yyan7/work/tools/pinsight/src/HIP_trace.config.install) exists with 50+ events defined (kernel, memory, stream, graph, etc.), but the runtime tracing backend is not yet implemented.

---

## 2. Extensible Domain Registration (DSL)

Domains are registered at initialization via a compile-time DSL macro system in [trace_domain_dsl.h](file:///home/yyan7/work/tools/pinsight/src/trace_domain_dsl.h):

```c
TRACE_DOMAIN_BEGIN("OpenMP", TRACE_EVENT_ID_NATIVE)
  TRACE_PUNIT("team", 0, MAX_TEAMS, omp_get_team_num)
  TRACE_PUNIT("thread", 0, MAX_THREADS, omp_get_thread_num)
  TRACE_SUBDOMAIN_BEGIN("parallel")
    TRACE_EVENT("omp_parallel_begin", 1, native_id, callback)
  TRACE_SUBDOMAIN_END()
TRACE_DOMAIN_END()
```

This populates `domain_info_table[]` with events, punits, and subdomains — enabling the config parser to validate event names and punit ranges at parse time.

---

## 3. Tracing Backend: LTTng UST

All trace data is emitted via [LTTng UST](https://lttng.org) tracepoints, providing:

- **Asynchronous collection**: Tracepoints write to per-CPU shared-memory ring buffers; a separate daemon writes to disk — no direct I/O in the application's hot path
- **Zero overhead when no session**: If no `lttng` session is active, tracepoint calls hit a fast-path no-op
- **CTF output format**: Traces are in Common Trace Format, compatible with babeltrace and Trace Compass

Tracepoint providers are defined in:
- [ompt_lttng_ust_tracepoint.h](file:///home/yyan7/work/tools/pinsight/src/ompt_lttng_ust_tracepoint.h) — OpenMP events
- [pmpi_lttng_ust_tracepoint.h](file:///home/yyan7/work/tools/pinsight/src/pmpi_lttng_ust_tracepoint.h) — MPI events
- [cupti_lttng_ust_tracepoint.h](file:///home/yyan7/work/tools/pinsight/src/cupti_lttng_ust_tracepoint.h) — CUDA events
- [enter_exit_lttng_ust_tracepoint.h](file:///home/yyan7/work/tools/pinsight/src/enter_exit_lttng_ust_tracepoint.h) — Library lifecycle

---

## 4. Configuration System

### 4.1 Hierarchical Scopes

Configuration is organized from coarsest to finest:

| Scope | Controls | Example |
|-------|----------|---------|
| **Domain (global)** | Mode (OFF/MONITORING/TRACING), punit range | `[OpenMP.global]` |
| **Domain (default)** | Event enable/disable defaults | `[OpenMP.default]` |
| **Domain (punit)** | Per-punit-set event overrides | `[OpenMP.thread(0-3)]` |
| **Lexgion (default)** | Global rate defaults | `[Lexgion.default]` |
| **Lexgion (domain-default)** | Per-domain lexgion defaults | `[Lexgion(OpenMP).default]` |
| **Lexgion (address)** | Per-code-region rate + events | `[Lexgion(0x4010bd)]` |

### 4.2 Configuration Sources

1. **Environment variables** — Domain modes (`PINSIGHT_TRACE_OPENMP=OFF`), rate sampling (`PINSIGHT_TRACE_RATE=0:100:1`), energy/backtrace toggles
2. **Config file** — Enhanced INI format with SET/RESET/REMOVE actions, inheritance, and punit constraints

### 4.3 Config File Features
- **3-action model**: SET (merge), RESET (revert to defaults), REMOVE (delete config)
- **Inheritance**: Lexgion sections can inherit events from domain defaults (`[Lexgion(0x400500)]: OpenMP.default, MPI.default`)
- **Punit constraints**: Cross-domain punit filtering (`[OpenMP.thread(0-3)]: OpenMP.default: MPI.rank(0), CUDA.device(0)`)
- **Multi-address lexgions**: `[Lexgion(0x400500, 0x400600, 0x400700)]`
- **Wildcard removal**: `[REMOVE OpenMP.thread(*)]`

---

## 5. Three Domain Trace Modes

| Mode | Description | Overhead |
|------|-------------|----------|
| **OFF** | Callbacks deregistered at runtime level | Zero per-event |
| **MONITORING** | Bookkeeping active (lexgion tracking, counters); no LTTng output | Low |
| **TRACING** | Full LTTng tracepoint emission | Normal |

Per-domain deregistration mechanisms:
- **OpenMP**: `ompt_set_callback(event, NULL)` — runtime stops dispatching
- **CUDA**: `cuptiEnableCallback(0, ...)` — native CUPTI disable
- **MPI**: Early-return guard (PMPI wrappers can't be deregistered at link time)

### Benchmark Results (Jacobi 512×512, 6-core Xeon)

| Config | 1T | 4T | 6T |
|--------|-----|-----|-----|
| OFF | +0.3% | −0.7% | +3.1% |
| MONITORING | −0.1% | +2.5% | −2.2% |
| TRACING (no session) | +0.5% | +2.4% | +4.8% |
| TRACING (with session) | +0.6% | +6.3% | +23.9% |
| Rate-limited (50:100) | +0.1% | +1.3% | +10.6% |

---

## 6. Rate-Based Sampling

Per-lexgion rate control via a triple:

| Parameter | Description | Default |
|-----------|-------------|---------|
| `trace_starts_at` | Skip N executions before tracing | 0 |
| `max_num_traces` | Stop after N traces (−1 = unlimited) | −1 |
| `tracing_rate` | Trace 1-in-N executions | 1 |

A region executed 10,000 times with `rate=100, max=50` produces only **50 traces** — a 200× I/O reduction.

---

## 7. Automatic Mode Switching (`trace_mode_after`)

When `max_num_traces` is reached, domains can be automatically switched to a lower-overhead mode:

```ini
[Lexgion.default]
    max_num_traces = 100
    trace_mode_after = MONITORING                    # all domains
    trace_mode_after = OpenMP:MONITORING, MPI:OFF    # per-domain
```

Also configurable via env var: `PINSIGHT_TRACE_RATE=0:100:1:MONITORING`

### PAUSE Action

The `trace_mode_after` key also supports a **PAUSE** action for pause-analyze-resume workflows:

```ini
[Lexgion.default]
    max_num_traces = 100
    trace_mode_after = PAUSE:60:analyze_traces.sh:TRACING
```

| Field | Description |
|-------|-------------|
| `PAUSE` | Action keyword |
| `60` | Timeout in seconds (0 = wait indefinitely for SIGUSR1) |
| `analyze_traces.sh` | Script to launch (`-` = none) |
| `TRACING` | Resume mode after pause (default: MONITORING) |

When PAUSE fires:
1. **`lttng rotate`** is executed automatically to flush traces to a completed chunk
2. The **user script** is launched with arguments: `<chunk_path> <app_pid> <config_file>`
3. Execution **blocks** until SIGUSR1 or timeout
4. On resume, domain modes switch to the specified resume mode

The script can analyze traces and send `SIGUSR1` back to resume the app early with optimized settings.

Also configurable via env var: `PINSIGHT_TRACE_RATE=0:100:10:PAUSE:60:analyze_traces.sh:TRACING`

## 8. Runtime Reconfiguration (SIGUSR1)

Live reconfiguration without restarting the application:

1. Edit config file
2. `kill -USR1 <pid>`
3. PInsight re-reads config, registers/deregisters callbacks dynamically

Uses a flag-based async-signal-safe mechanism: the signal handler sets `config_reload_requested`; actual reload happens at the next callback safe point.

---

## 9. Low-Overhead Design

| Technique | Description |
|-----------|-------------|
| **Lazy config resolution** | Thread-local cached config pointer validated against `trace_config_change_counter` — one integer compare per event |
| **Hierarchical early exit** | domain mode → event enabled → punit match → lexgion rate — each level can skip remaining work |
| **Nesting optimization** | Common nested regions (work, masked) piggyback on parent's trace decision via thread-local `enclosing_work_lgp` |
| **Asynchronous I/O** | LTTng ring-buffer decouples trace emission from disk I/O |
| **Zero-overhead OFF** | Real callback deregistration at the runtime level |

---

## 10. Additional Features

| Feature | Description | Build Flag |
|---------|-------------|------------|
| **Energy monitoring** | Intel RAPL via sysfs powercap (cores, GPU, package, RAM, psys) | `PINSIGHT_ENERGY` |
| **Backtrace capture** | Stack backtrace embedded in trace records | `PINSIGHT_BACKTRACE` |
| **Lexgion bookkeeping** | Per-thread lexgion stack with push/pop, counter tracking, code-region identification by address + type | Always on |
| **Library lifecycle tracing** | Enter/exit tracepoints via constructor/destructor attributes | Always on |

---

## 11. Analysis and Visualization

| Tool | Purpose |
|------|---------|
| **babeltrace** | CLI text dump of CTF traces |
| **Trace Compass** | Eclipse-based GUI for trace analysis and visualization |
| **PEAM** | [Python-based analysis scripts](https://github.com/passlab/peam) for PInsight traces |
| **trace.sh** | Helper script for LTTng session setup and trace collection |

---

## 12. Build Options

| Option | Default | Description |
|--------|---------|-------------|
| `PINSIGHT_OPENMP` | TRUE | OpenMP OMPT tracing |
| `PINSIGHT_MPI` | FALSE | MPI PMPI tracing |
| `PINSIGHT_CUDA` | TRUE | CUDA CUPTI tracing |
| `PINSIGHT_ENERGY` | FALSE | RAPL energy monitoring |
| `PINSIGHT_BACKTRACE` | TRUE | Stack backtrace in traces |

---

## 13. Features Not Yet Implemented / Partial

| Feature | Status |
|---------|--------|
| **HIP/ROCm tracing** | Config template exists; runtime backend not implemented |
| **MPI coverage** | 8 of 40+ defined events have PMPI wrappers (Init, Send, Recv, Barrier, Reduce, Allreduce, Finalize) |
| **CUDA mode deregistration** | `pinsight_sync_cuda_callbacks()` exists but not fully wired into SIGUSR1 reload path |
| **User-defined API tracing** | `USER_LEXGION` class defined in enum but no implementation |
| **Energy integration with traces** | RAPL reading functions exist but not integrated into LTTng tracepoints |
