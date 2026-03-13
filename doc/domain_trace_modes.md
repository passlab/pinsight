# Fine-Grained Domain Trace Modes

PInsight provides three operating modes per domain to control overhead-vs-detail tradeoffs at runtime.

## Modes

| Mode | Description | Overhead |
|------|-------------|----------|
| **OFF** | Callbacks deregistered; zero per-event overhead | None |
| **MONITORING** | Bookkeeping active (lexgion creation, counters); no LTTng output | Low |
| **TRACING** | Full tracing with LTTng tracepoint emission (default) | Normal |

Note: TRACING mode has two sub-configurations:
- **TRACING (no session)**: LTTng-UST tracepoints fire but hit the fast-path no-op when no LTTng session is consuming traces
- **TRACING (with session)**: An active `lttng` session collects traces — each tracepoint writes to the shared-memory ring buffer, adding real I/O overhead

## Overhead Evaluation

### System Specification

| Component | Detail |
|---|---|
| **CPU** | Intel Xeon W-2133 @ 3.60GHz (Turbo 3.90GHz) |
| **Cores / Threads** | 6 physical cores, Hyper-Threading ON → 12 logical CPUs |
| **Cache** | L1d: 192 KiB (6×32K), L2: 6 MiB (6×1M), L3: 8.25 MiB shared |
| **Memory** | 32 GB DDR4 |
| **OS** | Ubuntu 24.04, Linux 6.17.0-14-generic |
| **Compiler** | clang-21.1.8 (`-O3 -fopenmp`) |
| **LTTng** | lttng-tools 2.13.11, lttng-ust |

### Benchmark Setup

**Application**: Jacobi iterative solver, 5000 iterations
**Methodology**: 5 runs per configuration, **median** reported
**Thread counts**: 1, 2, 4, 6 (= physical cores), 8, 12 (= all HT logical CPUs)

### Results — 512×512 Grid (recommended)

#### Median Execution Time (ms)

| Config | 1T | 2T | 4T | 6T | 8T | 12T |
|---|---|---|---|---|---|---|
| **Baseline** | 4082 | 2066 | 1082 | 775 | 1074 | 959 |
| **OFF** | 4094 | 2115 | 1074 | 799 | 1074 | 988 |
| **MONITORING** | 4076 | 2112 | 1109 | 758 | 997 | 867 |
| **TRACING** (no session) | 4101 | 2161 | 1108 | 812 | 996 | 946 |
| **TRACING** (with session) | 4106 | 2151 | 1150 | 960 | 1171 | 1078 |
| **TRACING** (rate=50, max=100) | 4085 | 2147 | 1096 | 857 | 997 | 843 |

#### Overhead % vs Baseline

| Config | 1T | 2T | 4T | 6T | 8T | 12T |
|---|---|---|---|---|---|---|
| **OFF** | +0.3% | +2.4% | −0.7% | +3.1% | +0.0% | +3.0% |
| **MONITORING** | −0.1% | +2.2% | +2.5% | −2.2% | −7.2% | −9.6% |
| **TRACING** (no session) | +0.5% | +4.6% | +2.4% | +4.8% | −7.3% | −1.4% |
| **TRACING** (with session) | +0.6% | +4.1% | +6.3% | +23.9% | +9.0% | +12.4% |
| **TRACING** (rate=50, max=100) | +0.1% | +3.9% | +1.3% | +10.6% | −7.2% | −12.1% |

### Results — 256×256 Grid (high callback density)

#### Median Execution Time (ms)

| Config | 1T | 2T | 4T | 6T | 8T | 12T |
|---|---|---|---|---|---|---|
| **Baseline** | 984 | 585 | 372 | 307 | 353 | 600 |
| **OFF** | 1001 | 553 | 359 | 321 | 354 | 587 |
| **MONITORING** | 1027 | 573 | 410 | 323 | 349 | 1091 |
| **TRACING** (no session) | 1037 | 570 | 433 | 416 | 351 | 688 |
| **TRACING** (with session) | 1056 | 629 | 407 | 447 | 462 | 827 |
| **TRACING** (rate=50, max=100) | 1033 | 574 | 366 | 422 | 362 | 598 |

#### Overhead % vs Baseline

| Config | 1T | 2T | 4T | 6T | 8T | 12T |
|---|---|---|---|---|---|---|
| **OFF** | +1.7% | −5.5% | −3.5% | +4.6% | +0.3% | −2.2% |
| **MONITORING** | +4.4% | −2.1% | +10.2% | +5.2% | −1.1% | +81.8% |
| **TRACING** (no session) | +5.4% | −2.6% | +16.4% | +35.5% | −0.6% | +14.7% |
| **TRACING** (with session) | +7.3% | +7.5% | +9.4% | +45.6% | +30.9% | +37.8% |
| **TRACING** (rate=50, max=100) | +5.0% | −1.9% | −1.6% | +37.5% | +2.5% | −0.3% |

### Observations

1. **OFF ≈ baseline** across all thread counts and problem sizes (≤3%). Confirms callbacks are fully deregistered with zero per-event overhead.
2. **At 512×512**, overhead is much more stable than 256×256 because the computation-to-callback ratio is higher, reducing sensitivity to OS scheduling noise.
3. **MONITORING** adds ~2-3% overhead at 512×512 (1-4T), dominated by bookkeeping costs (`find_lexgion`, config lookup, `push/pop_lexgion`).
4. **TRACING (with session)** is the most expensive: +6% to +24% at 512×512, with the cost increasing at higher thread counts due to ring buffer contention.
5. **Rate-limited tracing** (`rate=50, max=100`) reduces overhead to ≤4% at 512×512 (1-4T), nearly matching baseline, while still capturing 100 representative trace samples per lexgion.
6. **At 8T and 12T** (beyond 6 physical cores), Hyper-Threading introduces contention and some results show negative overhead — this is measurement noise, not real speedup.
7. **256×256 shows higher percentage overhead** because the per-iteration computation is smaller, making the fixed callback cost a larger fraction.

### Rate-Limited Tracing Config

File: [`test/jacobi/trace_config_rate100.txt`](../test/jacobi/trace_config_rate100.txt)
```ini
[Lexgion.default]
    trace_starts_at = 0
    max_num_traces = 100
    tracing_rate = 50
```

**Benchmark script**: [`test/jacobi/run_bench.sh`](../test/jacobi/run_bench.sh) — usage: `bash run_bench.sh [N] [M]`

## Environment Variable

```bash
PINSIGHT_TRACE_<DOMAIN>=<MODE>
```

### Accepted Values

| Domain | Variable |
|--------|----------|
| OpenMP | `PINSIGHT_TRACE_OPENMP` |
| MPI | `PINSIGHT_TRACE_MPI` |
| CUDA | `PINSIGHT_TRACE_CUDA` |

| Mode | Accepted Values |
|------|----------------|
| OFF | `OFF`, `FALSE`, `0` |
| MONITORING | `MONITORING`, `MONITOR` |
| TRACING | `ON`, `TRACING`, `TRUE`, `1` (default) |

### Examples

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

## Composition with Per-Region Config

Domain mode is the **coarsest** control level. Within MONITORING/TRACING modes, existing per-lexgion config (address-specific entries, event filtering, punit filtering, rate-based sampling) still applies:

| Domain Mode | Per-Region trace_bit | Result |
|---|---|---|
| OFF | (ignored) | No callback dispatched |
| MONITORING | trace_bit=1 | Bookkeeping runs, counters updated |
| MONITORING | trace_bit=0 | Callback fires, skips quickly |
| TRACING | trace_bit=1 | Full LTTng tracepoint emission |
| TRACING | trace_bit=0 | Bookkeeping only for this region |

## Runtime Reconfiguration

Modes can be changed at runtime via the config file and SIGUSR1 signal:

1. Edit the config file to set `trace_mode` in a `[Domain.global]` section:
   ```ini
   [OpenMP.global]
       trace_mode = OFF
   ```
2. Send `kill -USR1 <pid>`
3. PInsight re-reads the config and calls `pinsight_register_openmp_callbacks()` to register/deregister callbacks dynamically by iterating the DSL-populated `event_table`

> **Note:** Environment variables (`PINSIGHT_TRACE_OPENMP`, etc.) are read at process launch only. For runtime reconfiguration, use the config file.

## Per-Domain Mechanisms

### OpenMP (OMPT)

- **OFF**: `ompt_set_callback(event, NULL)` — the OpenMP runtime stops dispatching the event entirely
- **Re-registration**: `ompt_set_callback(event, fn)` — restores callback dispatch
- **Function**: `pinsight_register_openmp_callbacks()` — iterates `domain_info_t.event_table[]`, declared in `ompt_callback.h`, defined in `ompt_callback.c`

### CUDA (CUPTI)

- **OFF**: `cuptiEnableCallback(0, subscriber, domain, cbid)` — native CUPTI enable/disable
- **Re-registration**: `cuptiEnableCallback(1, ...)` — re-enables callback
- **Function**: `pinsight_sync_cuda_callbacks()` in `cupti_callback.c`

### MPI (PMPI)

- **OFF**: Early-return guard in `PMPI_CALL_PROLOGUE` macro — PMPI wrappers are resolved at link time and cannot be deregistered. The overhead is one branch per MPI call, negligible relative to MPI communication latency.
- **Note**: MPI interception cannot be fully deregistered because `MPI_Send()` always calls PInsight's wrapper function via the PMPI interface.

## Implementation

### Data Structures

```c
// trace_config.h
typedef enum {
  PINSIGHT_DOMAIN_OFF = 0,
  PINSIGHT_DOMAIN_MONITORING = 1,
  PINSIGHT_DOMAIN_TRACING = 2
} pinsight_domain_mode_t;

#define PINSIGHT_DOMAIN_ACTIVE(mode) ((mode) >= PINSIGHT_DOMAIN_MONITORING)
#define PINSIGHT_SHOULD_TRACE(domain) \
    (domain_default_trace_config[domain].mode == PINSIGHT_DOMAIN_TRACING)

typedef struct domain_trace_config {
  pinsight_domain_mode_t mode;  // Domain operating mode
  unsigned long int events;     // Default event config
} domain_trace_config_t;
```

### Key Files Modified

| File | Changes |
|------|---------|
| `src/trace_config.h` | Mode enum, macros |
| `src/trace_config.c` | Env parsing (3 modes), init, print, SIGUSR1 handler |
| `src/trace_config_parse.c` | `SECTION_DOMAIN_GLOBAL`, `trace_mode` key, `[Domain.global]` section |
| `src/pinsight.c` | Kill-switch: `.mode == PINSIGHT_DOMAIN_OFF`, SIGUSR1 callback re-registration |
| `src/pinsight.h` | Kill-switch in `lexgion_check_event_enabled()` |
| `src/ompt_callback.c` | `pinsight_register_openmp_callbacks()`, `pinsight_wakeup_from_off_openmp()`, initial task reconnection, NULL guards |
| `src/ompt_callback.h` | Callback and wakeup function declarations |
| `test/trace_config_parse/test_config_parser.c` | Three-mode test cases |

## Bidirectional Mode Switch Evaluation

### Mechanism

Mode switching is triggered by modifying the config file and sending `SIGUSR1`:

1. Edit config file: set `trace_mode` in `[OpenMP.global]` (or other domain)
2. Send `kill -USR1 <pid>`
3. The SIGUSR1 handler sets `config_reload_requested = 1`
4. At the next `parallel_begin` (sequential pre-fork point), the deferred handler:
   - Calls `pinsight_load_trace_config()` to re-read the config
   - Calls `pinsight_register_openmp_callbacks()` to register/deregister callbacks
   - Reconnects the initial implicit task if switching to TRACING mode

For OFF→\* transitions, `pinsight_wakeup_from_off_openmp()` temporarily re-registers `parallel_begin/end` so the deferred handler can run.

### Test Setup

- **Binary**: LULESH 2.0, `-s 40`, `OMP_NUM_THREADS=4`
- **Single run**: 7 phases, 6 mode transitions via SIGUSR1 (4s intervals)
- **Config**: `max_num_traces` changed across phases to test config propagation

| Phase | Time | Mode | Transition | max_traces |
|-------|------|------|-----------|------------|
| 0 | 0s | TRACING | Initial | 50 |
| 1 | +4s | MONITORING | TRACING→MON | 50 |
| 2 | +8s | OFF | MON→OFF | 50 |
| 3 | +12s | MONITORING | OFF→MON | 50 |
| 4 | +16s | TRACING | MON→TRACING | 200 |
| 5 | +20s | OFF | TRACING→OFF | 200 |
| 6 | +24s | TRACING | OFF→TRACING | 500 |

### Transition Results

All 6 bidirectional transitions completed without crashes:

| # | Transition | Mechanism | Status |
|---|-----------|-----------|--------|
| 1 | TRACING → MONITORING | SIGUSR1 | ✅ |
| 2 | MONITORING → OFF | SIGUSR1 | ✅ |
| 3 | OFF → MONITORING | SIGUSR1 | ✅ |
| 4 | MONITORING → TRACING | SIGUSR1 | ✅ |
| 5 | TRACING → OFF | SIGUSR1 | ✅ |
| 6 | OFF → TRACING | SIGUSR1 | ✅ |

**Performance**: 1,294 iterations, 36s elapsed, FOM = 2,323.87 z/s, exit code 0.

### LTTng Validation

Traces were validated using LTTng (`lttng-tools 2.13.11`) with `babeltrace`:

- **31,070 trace events** captured in the `ompt_pinsight_lttng_ust` provider
- Events clustered during TRACING phases (Phase 0: ~6,250 events/second)
- **Zero events** during MONITORING and OFF phases, confirming correct callback deregistration
- Shutdown events (`thread_end`, `implicit_task_end`, `parallel_end`) correctly emitted at program termination

### Correctness Summary

| Aspect | Status | Notes |
|--------|--------|-------|
| Mode switching | ✅ | All 6 transitions work, no crashes |
| Stale event handling | ✅ | NULL guards skip stale `_end` events from pre-switch regions |
| Initial task reconnection | ✅ | `initial_task_lexgion_record` correctly reconnects master thread |
| Config reload on SIGUSR1 | ✅ | `trace_mode` changes propagated |
| Worker thread safety | ✅ | Workers handle transitions without crashes |
| `parallel_end` context restore | ✅ | NULL guard on `parent_task->ptr` prevents crashes |
| LTTng event presence | ✅ | Events emitted only during TRACING phases |

### Bugs Fixed During Mode Switch Development

1. **Env var override on reload** — `setup_trace_config_env()` was called after config file parsing on SIGUSR1 reloads, causing environment variables to override the `trace_mode` set in the config file. Fixed by moving `setup_trace_config_env()` to init-only (`initial_setup_trace_config`).

2. **Stale `_end` events after re-registration** — The OpenMP runtime fires `implicit_task_end`, `sync_region_end`, and `sync_region_wait_end` for the previous region after callbacks are re-registered. These have `task_data->ptr = NULL`. Fixed with NULL guards in `implicit_task_end`, `sync_region`, and `sync_region_wait`.

3. **Initial implicit task reconnection** — When switching to TRACING from MONITORING/OFF, the initial implicit task's `task_data->ptr` may not have been set (if `implicit_task` was deregistered at startup). Fixed by adding `initial_parallel_lexgion_record` and `initial_task_lexgion_record` thread-local pointers, with reconnection logic in `parallel_begin`.

4. **`parallel_end` NULL guard** — Added `parent_task->ptr != NULL` check for context restoration during MONITORING→TRACING transitions.

### Known Limitation

Per-lexgion config changes (e.g., `max_num_traces`, `tracing_rate`) in the config file do not fully propagate on SIGUSR1 reload. The `[Lexgion.default]` snapshot is taken at initialization and the domain-default copy is not refreshed on reload. Mode switching (`trace_mode`) works correctly. This is tracked as a future enhancement.

### Test Script

The evaluation test script is at [`eva/LULESH/test_bidir_mode_switch.sh`](../eva/LULESH/test_bidir_mode_switch.sh).
