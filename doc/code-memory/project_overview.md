---
name: project-overview
description: "What PInsight is, its SC'26 paper context, key features, and build system"
metadata: 
  node_type: memory
  type: project
  originSessionId: 9426b4f4-624f-4593-9d09-ae7065a7ae13
---

PInsight is a lightweight, dynamic tracing and in-situ performance analysis framework for parallel HPC applications (OpenMP, MPI, CUDA). Submitted to SC'26.

**Why:** Enable self-adaptive performance optimization — the application can analyze its own trace data and tune itself without stopping or human intervention.

**How to apply:** When suggesting new features or changes, align with the in-situ, closed-loop, low-overhead philosophy. Changes that add overhead without clear benefit are likely unwelcome.

## Core mechanism
- Intercepts runtime events via OMPT (OpenMP), PMPI (MPI), CUPTI (CUDA), and now sys.monitoring (Python PEP 669)
- Redirects them to LTTng UST for high-performance async trace collection
- Single shared library `libpinsight.so`, loaded via `LD_PRELOAD`

## Key features
1. **4-mode trace hierarchy**: OFF → STANDBY → MONITORING → TRACING (each adds exactly one cost layer). OFF is permanent/irreversible.
2. **Rate-limited tracing**: triple (trace_starts_at, max_num_traces, tracing_rate) per lexgion
3. **INTROSPECT**: closed-loop — app pauses, rotates traces, runs analysis script, resumes with new config
4. **Cyclic INTROSPECT**: uses generation counter so all lexgions reset trace_counter at cycle start
5. **Runtime reconfiguration via SIGUSR1**: hot-reload config file without stopping
6. **Application Knobs**: named config values the app can query at runtime (`pinsight_get_knob_int`)

## Build system
CMakeLists.txt; produces `build/libpinsight.so`.
Options: PINSIGHT_OPENMP (default TRUE), PINSIGHT_MPI (TRUE), PINSIGHT_CUDA (TRUE), PINSIGHT_ENERGY (FALSE), PINSIGHT_BACKTRACE (FALSE), PINSIGHT_PYTHON (FALSE).
Python support builds a separate `_pinsight_python` C extension module.
