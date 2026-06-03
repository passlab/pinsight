---
name: architecture
description: "PInsight core data structures, modules, and component relationships"
metadata: 
  node_type: memory
  type: project
  originSessionId: 9426b4f4-624f-4593-9d09-ae7065a7ae13
---

## Core abstractions

**Lexgion** (`lexgion_t` in pinsight.h): A "lexical region" — a source-code region identified by its binary `codeptr_ra` + type + class. Per-thread; no sharing between threads. Tracks execution count, trace count, rate-control state, and resolved trace config. The LRU search uses a `recent_lexgion` index for O(1) amortized lookup.

**Lexgion record** (`lexgion_record_t`): One runtime instance of a lexgion on a thread's stack. Forms a parent-pointer linked list (stack). Up to `MAX_LEXGION_STACK_DEPTH=16` deep.

**Thread data** (`pinsight_thread_data_t`): TLS struct per thread. Contains the lexgion cache array (512 max), the runtime stack, and thread identity (global_thread_num, omp_thread_num, etc.).

**Domain** (`domain_info_t`, `domain_trace_config_t`): Represents one tracing domain (OpenMP, MPI, CUDA, Python). Has subdomains, events (up to 64 via 64-bit bitset), punits (parallel units), and a 4-mode operating mode.

**Trace config** (`lexgion_trace_config_t`): 3-level hierarchy:
  1. Address-specific: `lexgion_address_trace_config[]` (keyed by codeptr)
  2. Domain default: `lexgion_domain_default_trace_config[domain]`
  3. Global default: `lexgion_default_trace_config`
  Config is lazily resolved and cached per-lexgion; `trace_config_change_counter` detects staleness.

**trace_mode_after_t**: Unified auto-trigger action. Holds per-domain target modes, introspect flag, timeout, script path, a `fired` atomic latch, and a `generation` counter for cyclic INTROSPECT.

**App Knobs** (`app_knob_t` in app_knob.h): Named int/double/string values set by the config file and queried by the application at runtime. Max 64 knobs, updated via SIGUSR1 config reload.

## Key source files

| File | Role |
|------|------|
| src/pinsight.h / pinsight.c | Core lexgion lifecycle (begin/end/push/pop), trace-bit logic |
| src/trace_config.h / trace_config.c | Config data structures, domain init, punit matching |
| src/trace_config_parse.c | INI-style config file parser |
| src/trace_domain_loader.c / trace_domain_dsl.h | DSL macros for declaring domains/subdomains/events/punits |
| src/trace_domain_OpenMP.h | OpenMP domain definition (DSL macro block) |
| src/trace_domain_MPI.h | MPI domain definition |
| src/trace_domain_CUDA.h | CUDA domain definition |
| src/trace_domain_Python.h | Python domain definition |
| src/ompt_callback.c | OMPT callbacks → LTTng tracepoints |
| src/pmpi_mpi.c | PMPI wrappers → LTTng tracepoints |
| src/cupti_callback.c | CUPTI subscriber callbacks → LTTng tracepoints |
| src/pysysmon_callback.c | Python sys.monitoring callbacks → LTTng tracepoints (PEP 669) |
| src/pinsight_control_thread.c | Dedicated control thread: SIGUSR1, INTROSPECT, mode changes |
| src/app_knob.h / app_knob.c | Application Knobs: named runtime config values |
| src/bitset.c / bitset.h | Bit-set implementation for event enable/disable |
| src/backtrace.c / backtrace.h | Optional GNU stack backtrace in trace records |
| src/rapl.c / rapl.h | Intel RAPL energy monitoring (optional) |

## Control thread
Dedicated `pthread` (pinsight_control_thread.c). Sleeps on `sem_wait`. Woken by:
- `SIGUSR1` → config reload
- auto-trigger → mode change or INTROSPECT
Handles: script spawn via `posix_spawn`, app pause/resume via condvar, `cuptiEnableCallback` calls (process-global, thread-safe), generation counter increment for cyclic INTROSPECT.

## Thread-ID namespaces (non-overlapping)
- OpenMP threads: 0+
- CUDA threads: 2000+
- Python threads: 3000+

## LTTng tracepoint naming
- OpenMP: `ompt_pinsight_lttng_ust:*`
- MPI: `pmpi_pinsight_lttng_ust:*`
- CUDA: `cupti_pinsight_lttng_ust:*`
- Python: `pysysmon_pinsight_lttng_ust:*`
