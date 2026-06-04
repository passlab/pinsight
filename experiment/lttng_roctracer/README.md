# HIP vecadd with ROCTracer Tracing

Step-by-step introduction to HIP programming and GPU tracing on AMD MI300A
(El Capitan / tuolumne), mirroring the `experiment/lttng_cupti/` CUDA example.

## Three phases

| Phase | Binary | What it demonstrates |
|-------|--------|----------------------|
| 1 | `vecadd` | Plain HIP: kernel, malloc, memcpy, sync |
| 2 | `vecadd_roctracer` | ROCTracer Callback + Activity API — printf output |
| 3 | `vecadd_lttng` | Same callbacks → LTTng UST tracepoints → CTF files |

## Build

```bash
module load rocm/7.2.1        # set ROCM_PATH
export PATH=$HOME/local/bin:$PATH
export LD_LIBRARY_PATH=$HOME/local/lib:$LD_LIBRARY_PATH

make all                      # builds all three phases
```

## Run

### Phase 1 — pure HIP

```bash
make run
# vecAdd PASSED (0 errors out of 1048576)
```

### Phase 2 — ROCTracer + printf

```bash
make run_roctracer
```

Expected output (one line per callback ENTER/EXIT + GPU activity records):

```
[ROCTracer] Initialized (printf tracing)
[ROCTracer CB] hipMemcpy ENTER  corr=1    dst=0x...  src=0x...  bytes=4194304  kind=HtoD
[ROCTracer CB] hipMemcpy EXIT   corr=1
...
[ROCTracer CB] hipLaunchKernel ENTER  corr=5    func=0x...  grid=(4096,1,1)  block=(256,1,1)  name=vecAdd
[ROCTracer CB] hipLaunchKernel EXIT   corr=5
[ROCTracer CB] hipDeviceSynchronize ENTER  corr=6
[ROCTracer CB] hipDeviceSynchronize EXIT   corr=6
[ROCTracer ACTIVITY] kernel  corr=5    begin=...  end=...  dur=... ns  device=0  name=vecAdd
[ROCTracer ACTIVITY] memcpy  corr=1    begin=...  end=...  dur=... ns  device=0  bytes=4194304
...
[ROCTracer] Finalized
vecAdd PASSED (0 errors out of 1048576)
```

**Key observations:**
- The correlation_id links each ENTER/EXIT callback pair to its GPU activity record.
- Activity records arrive asynchronously at `ROCTRACER_Fini()` flush time, after the callbacks.
- On MI300A (unified HBM), memcpy activity durations are near zero — no PCIe transfer.

### Phase 3 — ROCTracer + LTTng UST

```bash
make run_lttng
babeltrace2 vecadd_lttng_traces
```

The LTTng trace contains the same information as Phase 2's printf, but in binary CTF
format with nanosecond-accurate wall-clock timestamps, ready for Trace Compass analysis.

## ROCTracer API overview

### Callback API (`ACTIVITY_DOMAIN_HIP_API`)

Intercepts HIP runtime calls **synchronously** on the calling thread:

```
hipMemcpy() call
  → callback(domain, HIP_API_ID_hipMemcpy, api_data, NULL)
      api_data->phase == ACTIVITY_API_PHASE_ENTER  ← before execution
  → [actual memcpy runs]
  → callback(domain, HIP_API_ID_hipMemcpy, api_data, NULL)
      api_data->phase == ACTIVITY_API_PHASE_EXIT   ← after return
```

Key struct: `hip_api_data_t` (in `hip/amd_detail/hip_prof_str.h`):
- `correlation_id` — unique per call, links to activity record
- `phase` — ENTER or EXIT
- `args.hipMemcpy.{dst,src,sizeBytes,kind}`
- `args.hipLaunchKernel.{function_address,numBlocks,dimBlocks,stream}`

### Activity API (`ACTIVITY_DOMAIN_HIP_OPS`)

Captures **GPU-side** timestamps asynchronously via a pool:

```
roctracer_open_pool_expl(&props, &pool)  ← create pool with flush callback
roctracer_enable_domain_activity(HIP_OPS)
[HIP operations run on GPU ...]
roctracer_flush_activity_expl(pool)      ← flush at Fini or when buffer full
  → activity_flush_cb(begin, end, arg)   ← our callback, iterate records
      roctracer_next_record(rec, &rec)   ← variable-length record iterator
```

Activity record types (`rec->op`):
- `HIP_OP_ID_DISPATCH` (0) — kernel: `rec->kernel_name`, GPU `begin_ns`/`end_ns`
- `HIP_OP_ID_COPY`     (1) — memcpy: `rec->bytes`, GPU `begin_ns`/`end_ns`

### CUPTI vs ROCTracer comparison

| Feature | CUPTI (NVIDIA) | ROCTracer (AMD) |
|---------|---------------|----------------|
| Callback subscribe | `cuptiSubscribe()` | `roctracer_enable_domain_callback()` |
| Enable per-API | `cuptiEnableCallback(1, sub, domain, cbid)` | enabled for whole domain |
| Callback data | `CUpti_CallbackData *` | `hip_api_data_t *` |
| Activity model | Pull (tool provides buffer) | Push (runtime fills pool) |
| Activity iterate | `cuptiActivityGetNextRecord()` | `roctracer_next_record()` |
| API IDs | `CUPTI_RUNTIME_TRACE_CBID_*` | `HIP_API_ID_hip*` |
| Domains | `CUPTI_CB_DOMAIN_RUNTIME_API` | `ACTIVITY_DOMAIN_HIP_API` |

## Hardware note: AMD Instinct MI300A (tuolumne / El Capitan)

The MI300A is a unified CPU+GPU APU — all 4 CPU die chiplets and the GPU chiplet
share the same HBM3 memory pool. There is no PCIe between CPU and GPU.

Consequence for tracing:
- `hipMemcpy(HtoD/DtoH)` are intra-HBM moves; activity records show `begin_ns ≈ end_ns`
- Do not interpret memcpy activity duration as bandwidth — use size/time from a real PCIe system
- Kernel launch and sync remain the primary meaningful timing signals
