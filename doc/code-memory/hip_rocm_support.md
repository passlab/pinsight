---
name: hip-rocm-support
description: "HIP/ROCm tracing domain for El Capitan (AMD MI300A APU): Phase 1-3 implemented on hip-rocm-support branch, pending build on El Capitan"
metadata: 
  node_type: memory
  type: project
  originSessionId: 5044dd6e-5ce1-45c6-baef-42d72dd84475
---

HIP is the 5th PInsight tracing domain, targeting the El Capitan supercomputer
(AMD EPYC Genoa CPUs + AMD Instinct MI300A APUs). Implementation lives on branch
`hip-rocm-support` (current HEAD: `ebbd76e`).

**Why:** El Capitan is a top-tier DOE/LLNL system. MI300A's unified HBM memory
architecture (no PCIe between CPU and GPU) makes cross-domain correlation
especially important — the motivation is the same as for Python + HIP on one timeline.

**How to apply:** Mirror CUDA patterns; be aware of the ROCTracer pool model
difference (push vs CUPTI's pull). MI300A memcpy events fire but have near-zero
duration — don't treat them as bandwidth indicators.

## Hardware context
- El Capitan node: AMD EPYC 9654 (Genoa, 96 cores/socket) + AMD Instinct MI300A APU
- MI300A: CPU + GPU chiplets on one package sharing 128 GB HBM3 — no PCIe, unified memory
- `hipMemcpy(HtoD/DtoH)` are intra-HBM copies; Activity records will show begin_ns ≈ end_ns
- Kernel launch and sync events remain the primary meaningful traces

## New files (branch: hip-rocm-support)

| File | Role |
|------|------|
| `src/trace_domain_HIP.h` | DSL domain definition: 10 subdomains, 52 events, dense IDs 0-51 |
| `src/roctracer_lttng_ust_tracepoint.h` | LTTng UST provider `roctracer_pinsight_lttng_ust` |
| `src/roctracer_callback.c` | ROCTracer callbacks + activity pool implementation |
| `src/HIP_trace.config.install` | Corrected event name reference config |

## Modified files
- `CMakeLists.txt` — `PINSIGHT_HIP` option, ROCm/ROCTracer find logic
- `src/trace_config.c` — `register_HIP_trace_domain()` in `constructor(101)`
- `src/enter_exit.c` — `LTTNG_ROCTRACER_Init/Fini` in constructor/destructor
- `src/pinsight_control_thread.c` — `pinsight_control_hip_apply_mode()` in `control_apply_all_modes()`

## Key design decisions

**Thread IDs:** HIP host threads use `global_thread_num` starting at 4000
(OpenMP=0+, CUDA=2000+, Python=3000+, HIP=4000+).

**Lexgion codeptr:** Kernel function pointer (`hipLaunchKernel.f`) used as
codeptr — unique per kernel, stable for process lifetime. Same role as
`cbInfo->symbolName` in CUPTI.

**Activity pool model (differs from CUPTI):**
- CUPTI: pull model — CUPTI calls `bufRequested` to get a buffer from the tool
- ROCTracer: push model — `roctracer_open_pool_expl(&props, &handle)` with
  `props.buffer_callback_fun = flush_cb`; always use `roctracer_next_record()`
  to advance (records are variable-length)

**Deferred activity init:** `hip_activity_init_once()` called on first callback
(same pattern as CUPTI) — a HIP context must exist before
`roctracer_enable_domain_activity()`.

**Clock calibration:** `hip_clock_calibration` tracepoint fired once. On MI300A,
CPU and GPU share the same clock; offset is expected to be ~0 but still emitted
for analysis tool alignment.

## Event ID map (dense, TRACE_EVENT_ID_INTERNAL)

| Subdomain | IDs | Key enabled-by-default events |
|-----------|-----|-------------------------------|
| device    | 0-5  | `HIP_device_synchronize` (1) |
| context   | 6-9  | — |
| stream    | 10-14 | `HIP_stream_synchronize` (12) |
| eventmgmt | 15-19 | — |
| malloc    | 20-24 | — |
| memcpy    | 25-31 | HtoD(25) DtoH(26) DtoD(27) HtoH(28) async(29) |
| kernel    | 32-34 | `HIP_kernel_launch` (32) |
| graph     | 35-38 | — |
| module    | 39-41 | — |
| others    | 42-51 | — |

## Build command
```
cmake -DPINSIGHT_HIP=TRUE [-DROCM_PATH=/opt/rocm] ..
```

## Energy/Power measurement (commit ebbd76e, 2026-05-29)
Design doc: `doc/energy_power_implementation_plan.md` (977 lines).
Two independent CMake features:
- `PINSIGHT_ENERGY` — energy snapshot at lexgion enter/exit (existing rapl.c pattern extended to HIP/ROCm)
- `PINSIGHT_POWER` — periodic polling in the control thread

Both are per-platform-type and per-socket/device selectable. Not yet implemented — doc only.

## Next steps (as of 2026-05-30)

### Phase 4 — Build validation on El Capitan (first priority)
Build against actual ROCm 6.x headers. These specific areas may need fixes:

- **`hip_api_data_t` field names** in `roctracer_callback.c`:
  - `api_data->args.hipLaunchKernel.f` — may be `.function_address` in some versions
  - `api_data->args.hipMemcpy.sizeBytes` — verify exact field name
  - `api_data->retval` — may be `api_data->args.hipMemcpy.__retval` or similar
- **`roctracer_record_t` field names**:
  - `rec->memcpy_info.kind` — may be `rec->copy_kind` or similar
  - `rec->kernel_name` — verify field exists and is populated for HIP_OPS records
  - `rec->bytes` — verify field name for memcpy size
  - `HIP_OP_ID_DISPATCH` / `HIP_OP_ID_COPY` — verify these constants exist in `<roctracer/roctracer_hip.h>`
- **`hipKernelNameRefByPtr`** — may be `hipKernelNameRef` (without stream arg) in some versions

Recommended: add `pinsight_hip_runtime_available()` dlopen guard (same pattern as
`pinsight_cuda_runtime_available()` in `trace_config.c`) to gracefully handle
systems where `libroctracer64.so` is not present.

### Phase 5 — Hardware counter support via rocprofiler-sdk
Use `rocprofiler-sdk` (not ROCTracer) for hardware performance counters — there is
no clean counter path in ROCTracer. The two libraries coexist in the same process.
Header: `rocprofiler-sdk/rocprofiler.h`, link: `-lrocprofiler-sdk`.
This is a separate, later phase — do not conflate with Phase 4.

### Testing sequence
1. Simple `hipVectorAdd` test — verify tracepoints appear in `lttng view`
2. Named lexgion config: `[Lexgion(HIP:vectorAdd)]` — verify kernel name matching
3. AMReX HIP port or similar real HPC app on El Capitan
4. Mixed Python + HIP workload — verify cross-domain timeline alignment

## Related memories
- [[python-support]] — shares the `global_thread_num` namespace (3000+), will run together on El Capitan
- [[architecture]] — lexgion_t, ROCL_LEXGION class constant (already defined in pinsight.h)
