# PInsight CUDA Support: Design and Implementation

> **Status**: Implemented and verified on A100 / Driver 580.126 / CUDA 13.0 / CUPTI 2025.3.1

---

## 1. Overview

PInsight instruments CUDA applications using two complementary CUPTI APIs that together provide complete performance visibility: the **Callback API** for CPU-side call-site tracking and the **Activity API** for GPU-side execution timing.  Neither API alone is sufficient; they are designed to work together, linked by a `correlationId`.

---

## 2. Why Two APIs Are Needed

### 2.1 The Fundamental Problem with Async CUDA

CUDA operations are inherently asynchronous.  When a host thread calls `cudaMemcpyAsync` or a kernel launch, the call returns almost immediately — the actual work is enqueued on a GPU stream and executes later.

```
Host thread:
  t=0µs  cudaMemcpyAsync(...) ─── returns in ~2µs (just enqueues)
  t=2µs  [host continues...]

GPU DMA engine:
  t=50µs  [actual H2D transfer begins]
  t=96µs  [transfer completes]
```

If we only trace the host-side call, we get ~2µs — the enqueue overhead.
The actual transfer time (46µs in this example) is completely invisible.

### 2.2 What the Callback API Provides

The CUPTI **Callback API** fires a synchronous callback on the host thread at `CUPTI_API_ENTER` (before the call) and `CUPTI_API_EXIT` (after it returns).  It provides:

| Information | Available | Notes |
|---|---|---|
| `codeptr` (call-site address) | ✅ | Via `__builtin_return_address(N)` — enables lexgion identity |
| `functionName` | ✅ | e.g. `"cudaMemcpyAsync"`, `"_Z6VecAddPKiS0_Pii"` |
| `correlationId` | ✅ | Links to Activity API record |
| `functionParams` | ✅ | Grid/block dims, stream, copy kind, size |
| CPU-side timestamp | ✅ | Via `cuptiGetTimestamp()` at callback time |
| **GPU execution start/end** | ❌ | Not available — call has returned before GPU runs |
| Rate control hook | ✅ | Lexgion begin/end enables per-site sampling |

### 2.3 What the Activity API Provides

The CUPTI **Activity API** records GPU-side execution timestamps in an internal buffer.  It fires a `bufferCompleted` callback (on a CUPTI-internal thread) when the buffer fills or is explicitly flushed.

| Information | Available | Notes |
|---|---|---|
| `start` / `end` (GPU timestamps) | ✅ | Actual GPU execution window |
| `bytes`, `copyKind` | ✅ | Transfer properties |
| `correlationId` | ✅ | Links to Callback API event |
| `deviceId`, `contextId`, `streamId` | ✅ | Execution context |
| `codeptr` (call-site) | ❌ | Not available — no call-stack at GPU execution time |
| Rate control hook | ❌ | Records all operations unconditionally |

### 2.4 Complementary Roles

The two APIs serve different, non-overlapping purposes:

```
cudaMemcpyAsync call (host thread, callback API):
  ┌─────────────────────────────────────────────────────┐
  │ codeptr → lexgion identity (which call site?)        │
  │ rate control → should we trace this invocation?      │
  │ correlationId = 42                                   │
  └─────────────────────────────────────────────────────┘

GPU DMA transfer (CUPTI activity API):
  ┌─────────────────────────────────────────────────────┐
  │ start_gpu → actual transfer start timestamp          │
  │ end_gpu   → actual transfer end timestamp            │
  │ correlationId = 42  ← same ID, links the two         │
  └─────────────────────────────────────────────────────┘
```

For **synchronous** `cudaMemcpy`, the callback API alone is sufficient (the call blocks until the transfer completes, so `CUPTI_API_EXIT` timestamp ≈ transfer end).  The Activity API will also record it, confirming the GPU-side timing — this is harmless redundancy.

For **asynchronous** operations (`cudaMemcpyAsync`, kernel launches), both APIs are essential.

---

## 3. Implementation

### 3.1 Callback API Implementation (`src/cupti_callback.c`)

#### Subscriber and always-active callbacks

```c
// LTTNG_CUPTI_Init
cuptiSubscribe(&subscriber,
               (CUpti_CallbackFunc)CUPTI_callback_lttng, NULL);

// Enable callbacks for all events we handle
cuptiEnableCallback(1, subscriber, CUPTI_CB_DOMAIN_RUNTIME_API,
                    CUPTI_RUNTIME_TRACE_CBID_cudaLaunchKernel_v7000);
cuptiEnableCallback(1, subscriber, CUPTI_CB_DOMAIN_RUNTIME_API,
                    CUPTI_RUNTIME_TRACE_CBID_cudaMemcpy_v3020);
cuptiEnableCallback(1, subscriber, CUPTI_CB_DOMAIN_RUNTIME_API,
                    CUPTI_RUNTIME_TRACE_CBID_cudaMemcpyAsync_v3020);
cuptiEnableCallback(1, subscriber, CUPTI_CB_DOMAIN_RUNTIME_API,
                    CUPTI_RUNTIME_TRACE_CBID_cudaDeviceSynchronize_v3020);
cuptiEnableCallback(1, subscriber, CUPTI_CB_DOMAIN_RUNTIME_API,
                    CUPTI_RUNTIME_TRACE_CBID_cudaStreamSynchronize_v3020);
// ... (cudaDeviceReset handled specially for activity flush)
```

Callbacks are **always active** for the program lifetime.  OFF mode is implemented as an early-return "killswitch" at the callback entry — `cuptiEnableCallback`/`cuptiEnableDomain` are not async-signal-safe and cannot be called from signal handlers.

#### Callback entry structure

Every CUDA API call flows through `CUPTI_callback_lttng`:

```c
void CUPTIAPI CUPTI_callback_lttng(void *userdata,
                                   CUpti_CallbackDomain domain,
                                   CUpti_CallbackId cbid,
                                   const CUpti_CallbackData *cbInfo) {

    // 1. Deferred reconfig: consume SIGUSR1 / mode_change flags
    //    Optimistic fast-path: volatile read first, atomic only if set
    if (cbInfo->callbackSite == CUPTI_API_ENTER) {
        if (config_reload_requested &&
            __atomic_exchange_n(&config_reload_requested, 0, __ATOMIC_SEQ_CST))
            pinsight_load_trace_config(NULL);
        if (mode_change_requested &&
            __atomic_exchange_n(&mode_change_requested, 0, __ATOMIC_SEQ_CST))
            /* mode already written; killswitch picks it up */;
    }

    // 2. Domain killswitch — OFF mode: single branch, near-zero overhead
    if (!PINSIGHT_DOMAIN_ACTIVE(domain_default_trace_config[CUDA_domain_index].mode))
        return;

    // 3. Only runtime API domain
    if (domain != CUPTI_CB_DOMAIN_RUNTIME_API) return;

    // 4. Thread init + one-shot clock calibration
    cuda_ensure_thread_init();
    cuda_emit_clock_calibration_once();

    // 5. Extract common info
    unsigned int cxtId, devId, correlationId;
    cuptiGetContextId(cbInfo->context, &cxtId);
    cuptiGetDeviceId(cbInfo->context, &devId);
    correlationId = cbInfo->correlationId;

    // 6. Per-event dispatch ...
}
```

#### Per-event dispatch (kernel launch example)

```c
if (cbid == CUPTI_RUNTIME_TRACE_CBID_cudaLaunchKernel_v7000) {
    // Calibrated depth 11 for A100/CUDA13 — see stack calibration section
    const void *codeptr = __builtin_return_address(11);

    if (cbInfo->callbackSite == CUPTI_API_ENTER) {
        // Extract launch params
        cudaLaunchKernel_v7000_params *p = cbInfo->functionParams;
        unsigned int streamId;
        cuptiGetStreamIdEx(context, p->stream, 0, &streamId);

        // Lexgion begin: finds or creates call-site record, rate control
        lexgion_record_t *record = lexgion_begin(
            CUDA_LEXGION, CUDA_EVENT_KERNEL_LAUNCH, codeptr);
        lexgion_t *lgp = record->lgp;
        lexgion_set_top_trace_bit_domain_event(lgp, CUDA_domain_index,
                                               CUDA_EVENT_KERNEL_LAUNCH);

        if (PINSIGHT_SHOULD_TRACE(CUDA_domain_index) && lgp->trace_bit) {
            uint64_t timeStamp;
            cuptiGetTimestamp(&timeStamp);  // CPU-side timestamp
            lttng_ust_tracepoint(cupti_pinsight_lttng_ust,
                cudaKernelLaunch_begin,
                devId, correlationId, timeStamp, codeptr, kernelName,
                &ctxStreamId, &dimension);
        }
    } else { // CUPTI_API_EXIT
        lexgion_t *lgp = lexgion_end(NULL);
        if (lgp && lgp->trace_bit) {
            lttng_ust_tracepoint(cupti_pinsight_lttng_ust,
                cudaKernelLaunch_end, ...);
            lexgion_post_trace_update(lgp);  // rate control + auto-trigger
        }
    }
    return;
}
```

The **lexgion** abstraction is central: each unique `codeptr` (call site) gets its own lexgion with independent counters, trace bits, and rate-control configuration.  This is what enables per-kernel-launch-site trace rate limiting.

#### Multi-thread handling

`pinsight_thread_data` is thread-local storage (`__thread`).  A lazy initialization pattern handles all thread cases:

```c
// Atomic counter starting at 2000 — avoids collision with
// OpenMP thread IDs (0..N-1) from on_ompt_callback_thread_begin
static _Atomic int cuda_thread_id_counter = 2000;

static inline void cuda_ensure_thread_init(void) {
    if (!pinsight_thread_data.initialized) {
        int tid = __atomic_fetch_add(&cuda_thread_id_counter, 1,
                                     __ATOMIC_RELAXED);
        init_thread_data(tid);
    }
}
```

| Thread type | Initialization | ID range |
|---|---|---|
| OpenMP threads | `on_ompt_callback_thread_begin` (OMPT) | 0..N-1 |
| Pure CUDA host threads | First CUPTI callback | 2000+ |
| CUPTI internal threads | First CUPTI callback | 2000+ |
| OpenMP thread making CUDA call | Already initialized — no-op | (preserved) |

When an OpenMP thread issues a CUDA call, the unified lexgion stack correctly nests the CUDA lexgion inside the active OpenMP lexgions:

```
Thread 3's stack during VecAdd kernel launch:
  [0] parallel_begin  (OPENMP_LEXGION)  ← pushed by OMPT parallel_begin
  [1] implicit_task   (OPENMP_LEXGION)  ← pushed by OMPT implicit_task_begin
  [2] cudaLaunchKernel (CUDA_LEXGION)   ← pushed at CUPTI_API_ENTER
       ← popped by lexgion_end() at CUPTI_API_EXIT
  [1] implicit_task   (OPENMP_LEXGION)  ← intact, unaffected
```

`lexgion_end()` is a simple LIFO pop — it pops the top regardless of class.  The CUPTI ENTER/EXIT guarantee is always paired, so CUDA lexgions always sit on top and are popped cleanly.

#### `__builtin_return_address` stack depth calibration

The `codeptr` for call-site identification is obtained by walking the call stack:

```c
const void *codeptr = __builtin_return_address(N);
```

The depth `N` is driver-dependent.  Calibrated on **A100 / Driver 580.126 / CUDA 13.0**:

| Event | Depth |
|---|---|
| `cudaMemcpy`, `cudaDeviceSynchronize`, `cudaStreamSynchronize` | **8** |
| `cudaLaunchKernel` | **11** (3 extra stub frames injected by `nvcc`) |

> **Warning**: These depths must be re-calibrated after driver or CUDA toolkit upgrades.

---

### 3.2 Activity API Implementation (`src/cupti_callback.c`)

#### Buffer management

```c
#define CUPTI_ACTIVITY_BUFFER_SIZE  (8 * 1024 * 1024)   // 8 MB per buffer
#define CUPTI_ACTIVITY_BUFFER_ALIGN 8

// CUPTI calls this when it needs storage for activity records
static void CUPTIAPI activity_bufferRequested(uint8_t **buffer, size_t *size,
                                              size_t *maxNumRecords) {
    *maxNumRecords = 0;  // unlimited
    *size = CUPTI_ACTIVITY_BUFFER_SIZE;
    posix_memalign((void **)buffer, CUPTI_ACTIVITY_BUFFER_ALIGN,
                   CUPTI_ACTIVITY_BUFFER_SIZE);
}

// CUPTI calls this when buffer is full or cuptiActivityFlushAll() is called
static void CUPTIAPI activity_bufferCompleted(CUcontext ctx, uint32_t streamId,
                                              uint8_t *buffer, size_t size,
                                              size_t validSize) {
    CUpti_Activity *record = NULL;
    do {
        if (cuptiActivityGetNextRecord(buffer, validSize, &record) != CUPTI_SUCCESS)
            break;
        switch (record->kind) {
        case CUPTI_ACTIVITY_KIND_MEMCPY: {
            CUpti_ActivityMemcpy6 *m = (CUpti_ActivityMemcpy6 *)record;
            lttng_ust_tracepoint(cupti_pinsight_lttng_ust, cudaMemcpyActivity,
                m->deviceId, m->correlationId,
                m->start, m->end, m->bytes,   // ← actual GPU timestamps
                (int)m->copyKind, m->contextId, m->streamId);
            break;
        }
        case CUPTI_ACTIVITY_KIND_KERNEL:
        case CUPTI_ACTIVITY_KIND_CONCURRENT_KERNEL: {
            CUpti_ActivityKernel10 *k = (CUpti_ActivityKernel10 *)record;
            lttng_ust_tracepoint(cupti_pinsight_lttng_ust, cudaKernelActivity,
                k->deviceId, k->correlationId,
                k->start, k->end,             // ← actual GPU execution window
                k->contextId, k->streamId, k->name);
            break;
        }
        }
    } while (1);
    free(buffer);
}
```

#### Activity kinds enabled

```c
// LTTNG_CUPTI_Init
cuptiActivityRegisterCallbacks(activity_bufferRequested, activity_bufferCompleted);
cuptiActivityEnable(CUPTI_ACTIVITY_KIND_MEMCPY);            // sync + async memcpy
cuptiActivityEnable(CUPTI_ACTIVITY_KIND_KERNEL);            // non-concurrent kernels
cuptiActivityEnable(CUPTI_ACTIVITY_KIND_CONCURRENT_KERNEL); // concurrent kernels (MPS)
```

#### Buffer flush lifecycle

The critical requirement is that `bufferCompleted` must fire **before LTTng stops**.  The call chain is:

```
User code:
  cudaDeviceReset()
       │
       ▼ CUPTI callback at CUPTI_API_ENTER
  cuptiActivityFlushAll(1)   ← blocking: waits for all bufferCompleted to finish
       │
       ▼ bufferCompleted fires (on CUPTI internal thread)
  lttng_ust_tracepoint(...)  ← LTTng still active ✓
       │
       ▼ cudaDeviceReset completes
  ~pinsight destructor (priority 102)
  cuptiActivityFlushAll(0)   ← catches any remaining records
  cuptiUnsubscribe(subscriber)

trace.sh:
  lttng stop                 ← only reaches here after program exits
```

The `cudaDeviceReset` callback is specifically handled (not a stub) to trigger the blocking flush:

```c
if (cbid == CUPTI_RUNTIME_TRACE_CBID_cudaDeviceReset_v3020) {
    if (cbInfo->callbackSite == CUPTI_API_ENTER)
        cuptiActivityFlushAll(1);  // 1 = blocking
    return;
}
```

> **Note for Castro/AMReX**: AMReX manages device lifetime and may not call `cudaDeviceReset`.  In that case, the destructor's `cuptiActivityFlushAll(0)` serves as the flush point.  To guarantee records are captured, AMReX applications should call `cudaDeviceReset()` before `MPI_Finalize()`.

---

## 4. The Timing Problem and its Solution

### 4.1 The Core Issue

When `bufferCompleted` fires, `lttng_ust_tracepoint()` stamps the event with the **CPU wall-clock at buffer delivery time** — not at the actual GPU operation time.  All activity records in the buffer carry the same (or nearby) LTTng event timestamp regardless of when the GPU operations actually ran.

```
Timeline:
  t=100ms  cudaMemcpyAsync H2D ─── CPU enqueue (callback fires here)
  t=100ms  cudaLaunchKernel    ─── CPU enqueue (callback fires here)
  t=102ms  cudaDeviceReset     ─── triggers cuptiActivityFlushAll(1)
  t=102ms  bufferCompleted fires ─── ALL activity tracepoints get t=102ms
                                     as LTTng event timestamp!
```

So in the LTTng trace stream, the `cudaMemcpyActivity` and `cudaKernelActivity` events appear at `t=102ms`, even though they actually ran between `t=100ms` and `t=102ms`.

### 4.2 The Solution: GPU Timestamps as Payload Fields

The actual GPU execution timestamps are stored in the **payload fields** of each activity tracepoint, not in the LTTng event timestamp:

```c
// Tracepoint definition
LTTNG_UST_TRACEPOINT_EVENT(cupti_pinsight_lttng_ust, cudaMemcpyActivity,
    LTTNG_UST_TP_ARGS(
        uint64_t, start_gpu,   // ← actual GPU start (CUPTI ns)
        uint64_t, end_gpu,     // ← actual GPU end   (CUPTI ns)
        ...
    ),
    LTTNG_UST_TP_FIELDS(
        lttng_ust_field_integer(uint64_t, start_gpu, start_gpu)
        lttng_ust_field_integer(uint64_t, end_gpu,   end_gpu)
        ...
    )
)
```

Analysis tools read `start_gpu`/`end_gpu` from the fields and ignore the LTTng event timestamp for GPU operations.

### 4.3 Clock Calibration

The `start_gpu`/`end_gpu` values are in CUPTI's timestamp domain.  The callback-API tracepoints use `cupti_timeStamp` (also from `cuptiGetTimestamp()`), so those are already comparable.  But to align with CPU domain events (OpenMP, MPI, which use `CLOCK_MONOTONIC`), we need the offset between clocks.

#### The calibration event

On the **first CUDA API call** (after LTTng probes are registered), a one-shot calibration event fires:

```c
static _Atomic int clock_calibration_done = 0;

static inline void cuda_emit_clock_calibration_once(void) {
    if (!clock_calibration_done &&
        __atomic_exchange_n(&clock_calibration_done, 1, __ATOMIC_SEQ_CST) == 0) {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        uint64_t monotonic_ns = (uint64_t)ts.tv_sec * 1000000000ULL
                              + (uint64_t)ts.tv_nsec;
        uint64_t cupti_ts;
        cuptiGetTimestamp(&cupti_ts);
        lttng_ust_tracepoint(cupti_pinsight_lttng_ust, cuda_clock_calibration,
                             monotonic_ns, cupti_ts);
    }
}
```

This is called **after** `cuda_ensure_thread_init()` in the callback, guaranteeing:
1. LTTng probes are registered (first tracepoint has already been processed)
2. Exactly one calibration event per run (atomic exchange prevents duplicates)
3. CLOCK_MONOTONIC and CUPTI timestamp sampled back-to-back for minimal skew

#### Real example from a vecadd trace

```
cuda_clock_calibration:
  clock_monotonic_ns  = 12_805_940_645_034     ← ns since system boot
  cupti_timestamp_ns  = 1_775_157_844_184_792_149  ← ns since Unix epoch

  offset = cupti_timestamp_ns - clock_monotonic_ns
         = 1_775_145_038_244_147_115 ns
         ≈ Unix timestamp 2026-04-02 (correct ✓)


cudaKernelLaunch_begin (correlationId=3):
  cupti_timeStamp = 1_775_157_844_184_799_457
  → CLOCK_MONOTONIC equivalent = 1_775_157_844_184_799_457 - offset
                                = 12_805_940_652_342 ns since boot

cudaKernelActivity (correlationId=3):
  start_gpu = 1_775_157_844_184_877_405  → kernel began 78µs after CPU launch
  end_gpu   = 1_775_157_844_184_884_477  → kernel ran for 7µs on GPU
```

The CPU launch call (`cudaKernelLaunch_begin` to `end`) spans ~84µs (CUDA API overhead).
The actual GPU execution (`start_gpu` to `end_gpu`) is 7µs.
The 78µs gap between CPU launch and GPU start is the driver scheduling latency.

#### How analysis tools use the calibration

```python
# From the cuda_clock_calibration event:
offset = cupti_timestamp_ns - clock_monotonic_ns

# Convert any GPU timestamp to CLOCK_MONOTONIC timeline:
def gpu_to_monotonic(ts):
    return ts - offset

# For the kernel above:
gpu_to_monotonic(start_gpu) = 1_775_157_844_184_877_405 - 1_775_145_038_244_147_115
                             = 12_805_940_730_290 ns since boot  ✓

# Note: within CUPTI, all timestamps (callback cupti_timeStamp AND
# activity start/end) are in the same epoch, so no conversion needed
# to compare them against each other — only needed for cross-domain
# correlation with OpenMP/MPI events.
```

---

## 5. LTTng Tracepoint Design

### 5.1 Callback API Tracepoints

All callback tracepoints use `COMMON_CUDA_ARG` which includes `devId`, `correlationId`, `cupti_timeStamp`, `codeptr`, and `func_name`.  They additionally use `COMMON_LTTNG_UST_TP_FIELDS_MPI_OMP` which includes thread-local context (MPI rank, OpenMP thread/team numbers).

| Tracepoint | Extra fields |
|---|---|
| `cudaKernelLaunch_begin` | `contextId`, `streamId`, grid/block dims (6 fields) |
| `cudaKernelLaunch_end` | (none) |
| `cudaMemcpy_begin` | `dst`, `src`, `count`, `cudaMemcpyKind` enum |
| `cudaMemcpy_end` | `return_val` |
| `cudaMemcpyAsync_begin` | `dst`, `src`, `count`, `cudaMemcpyKind`, `contextId`, `streamId` |
| `cudaMemcpyAsync_end` | `return_val` |
| `cudaDeviceSync_begin/end` | (none / `return_val`) |
| `cudaStreamSync_begin` | `contextId`, `streamId` |
| `cudaStreamSync_end` | `return_val` |

### 5.2 Activity API Tracepoints

Activity tracepoints do **not** use `COMMON_LTTNG_UST_TP_FIELDS_MPI_OMP` because `bufferCompleted` fires on a CUPTI internal thread where thread-local OpenMP variables are meaningless.  They use a minimal field set:

| Tracepoint | Fields |
|---|---|
| `cuda_clock_calibration` | `clock_monotonic_ns`, `cupti_timestamp_ns` |
| `cudaMemcpyActivity` | `devId`, `correlationId`, `start_gpu`, `end_gpu`, `bytes`, `cudaMemcpyKind`, `contextId`, `streamId` |
| `cudaKernelActivity` | `devId`, `correlationId`, `start_gpu`, `end_gpu`, `contextId`, `streamId`, `kernelName` |

### 5.3 LTTng Field Count Limit

LTTng UST has a 10-field-per-tracepoint limit in some versions.  The `cudaMemcpyAsync_begin` tracepoint already reaches this limit (noted in the header).  New tracepoints are designed to stay within this limit.

---

## 6. Event Flow for a Complete Per-Iteration Trace

For one iteration of a typical GPU compute loop (Castro-style: H2D → kernel → D2H):

```
LTTng trace events in chronological order:

1. cudaMemcpyAsync_begin  correlationId=1  codeptr=0x...E0B  (H2D, 200KB)
                          cupti_timeStamp = T_cpu_1
2. cudaMemcpyAsync_end    correlationId=1
3. cudaLaunchKernel_begin correlationId=2  codeptr=0x...E99  grid=196,block=256
                          cupti_timeStamp = T_cpu_2
4. cudaLaunchKernel_end   correlationId=2
5. cudaMemcpyAsync_begin  correlationId=3  codeptr=0x...EB2  (D2H, 200KB)
6. cudaMemcpyAsync_end    correlationId=3
7. cudaStreamSynchronize_begin  (host waits for all GPU work)
8. cudaStreamSynchronize_end    (returns when GPU done)

<< at cudaDeviceReset: cuptiActivityFlushAll(1) fires >>

9.  cudaMemcpyActivity  correlationId=1  start_gpu=T_g1  end_gpu=T_g2
                        bytes=200000  kind=HostToDevice
10. cudaKernelActivity  correlationId=2  start_gpu=T_g3  end_gpu=T_g4
                        kernelName="_Z8ComputePKfPfi"
11. cudaMemcpyActivity  correlationId=3  start_gpu=T_g5  end_gpu=T_g6
                        bytes=200000  kind=DeviceToHost

Analysis via correlationId:
  H2D transfer time  = T_g2 - T_g1  (from cudaMemcpyActivity #9)
  Kernel exec time   = T_g4 - T_g3  (from cudaKernelActivity #10)
  D2H transfer time  = T_g6 - T_g5  (from cudaMemcpyActivity #11)
  CPU-GPU scheduling gap = T_g1 - T_cpu_1  (launch → first GPU work)
```

---

## 7. Mode Switching and OFF Mode

CUDA domain mode switching uses the same three-mode system as OpenMP (`OFF`, `MONITORING`, `TRACING`), but with a fundamentally different mechanism.

### 7.1 Why CUPTI Callbacks Cannot Be Disabled at Runtime

`cuptiEnableCallback()` and `cuptiEnableDomain()` are **not async-signal-safe**.  They cannot be called from SIGUSR1 handlers or from within the deferred reconfig path safely.

### 7.2 The Killswitch Pattern

CUPTI callbacks are always registered and active.  OFF mode is implemented as an early return at the callback entry:

```c
// OFF mode: single branch, ~1ns overhead per CUDA API call
if (!PINSIGHT_DOMAIN_ACTIVE(domain_default_trace_config[CUDA_domain_index].mode))
    return;
```

The overhead is minimal — much less than the microsecond-scale cost of CUDA API calls themselves.

### 7.3 Deferred Reconfig Handler

Mode changes and config reloads are applied on the next CUDA API call:

```c
// Optimistic fast-path: volatile read (cheap) before atomic exchange (expensive)
if (config_reload_requested &&
    __atomic_exchange_n(&config_reload_requested, 0, __ATOMIC_SEQ_CST)) {
    pinsight_load_trace_config(NULL);
}
if (mode_change_requested &&
    __atomic_exchange_n(&mode_change_requested, 0, __ATOMIC_SEQ_CST)) {
    /* new mode already written by pinsight_fire_mode_triggers() */
}
```

SIGUSR1 sets `config_reload_requested`.  The auto-trigger (when a lexgion reaches `max_num_traces`) sets `mode_change_requested`.  Both are consumed safely at the next CUDA callback entry.

---

## 8. Source Files

| File | Role |
|---|---|
| `src/cupti_callback.c` | Main implementation: callbacks, Activity API, init/fini |
| `src/cupti_lttng_ust_tracepoint.h` | LTTng UST tracepoint definitions for all CUDA events |
| `src/trace_domain_CUDA.h` | CUDA domain index, event type constants |
| `src/enter_exit.c` | Constructor/destructor calling `LTTNG_CUPTI_Init`/`Fini` |
| `src/pinsight.h` | Thread-local data, lexgion stack, `lexgion_begin/end` |
| `test/cuda/vecadd_pinsight.cu` | Verification test (calls `cudaDeviceReset` for flush) |

---

## 9. Build Configuration

CUDA support is conditionally compiled:

```cmake
# CMakeLists.txt
option(PINSIGHT_CUDA "Enable CUDA/CUPTI instrumentation" OFF)
if(PINSIGHT_CUDA)
    target_sources(pinsight PRIVATE src/cupti_callback.c)
    target_link_libraries(pinsight cupti cuda)
    target_compile_definitions(pinsight PUBLIC PINSIGHT_CUDA)
endif()
```

---

## 10. Known Limitations and Future Work

| Item | Status | Notes |
|---|---|---|
| `__builtin_return_address` depth | Calibrated for A100/CUDA13 | Re-calibrate on driver upgrade; consider replacing with CUPTI `correlationId`-only approach |
| `cudaDeviceReset` requirement | Required for test programs | Production apps (Castro/AMReX) may not call it; buffer flush relies on destructor |
| Activity API for cudaGraphs | Not implemented | CUPTI_ACTIVITY_KIND_GRAPH_TRACE needed for graph-based launches |
| `bufferCompleted` thread | CUPTI internal thread | No OpenMP context; lexgion rate control not applied to activity records (all are recorded) |
| Clock offset calibration | One-shot at first callback | Sufficient for a single run; may drift for very long runs (rare) |
