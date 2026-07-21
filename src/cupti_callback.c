//
// Created by Yonghong Yan on 12/12/19.
// Restructured for full trace control (domain modes, rate control,
// introspection) — April 2026.
//

#include "pinsight.h"
#include "pinsight_control_thread.h"
#include "trace_config.h"
#include <cuda.h>
#include <cuda_runtime.h>
#include <cupti.h>
#include <cupti_activity.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

int CUDA_domain_index;
domain_info_t *CUDA_domain_info;
domain_trace_config_t *CUDA_trace_config;

#define LTTNG_UST_TRACEPOINT_CREATE_PROBES
#define LTTNG_UST_TRACEPOINT_DEFINE
#include "cupti_lttng_ust_tracepoint.h"

/* ================================================================
 * Event ID constants — must match the dense IDs assigned by
 * TRACE_EVENT_ID_INTERNAL in trace_domain_CUDA.h's DSL definition.
 * Events are numbered 0..N-1 in declaration order.
 * ================================================================ */
#define CUDA_EVENT_DEVICE_RESET 3
#define CUDA_EVENT_DEVICE_SYNCHRONIZE 4
#define CUDA_EVENT_STREAM_CREATE 5
#define CUDA_EVENT_STREAM_SYNCHRONIZE 7
#define CUDA_EVENT_MEMCPY_HTOD 12
#define CUDA_EVENT_MEMCPY_DTOH 13
#define CUDA_EVENT_MEMCPY_DTOD 14
#define CUDA_EVENT_MEMCPY_HTOH 15
#define CUDA_EVENT_MEMCPY_ASYNC 16
#define CUDA_EVENT_MALLOC 18
#define CUDA_EVENT_FREE 19
#define CUDA_EVENT_KERNEL_LAUNCH 23

/* ================================================================
 * Helper functions
 * ================================================================ */

int CUDA_get_device_id(void *arg) {
  int currentDevice;
  cudaGetDevice(&currentDevice);
  return currentDevice;
}

/**
 * Map cudaMemcpyKind to the corresponding CUDA domain event ID.
 */
static inline int cuda_memcpy_event_id(int kind) {
  switch (kind) {
  case cudaMemcpyHostToDevice:
    return CUDA_EVENT_MEMCPY_HTOD;
  case cudaMemcpyDeviceToHost:
    return CUDA_EVENT_MEMCPY_DTOH;
  case cudaMemcpyDeviceToDevice:
    return CUDA_EVENT_MEMCPY_DTOD;
  default:
    return CUDA_EVENT_MEMCPY_HTOH;
  }
}

/* Atomic counter for assigning unique IDs to non-OpenMP threads
 * (pure CUDA host threads, CUDA driver internal threads).
 * Starts at 2000 to avoid colliding with OpenMP thread IDs (0..N-1).
 * Each OS thread has its own TLS copy of pinsight_thread_data, so
 * the only shared state is this counter — hence the atomic. */
static _Atomic int cuda_thread_id_counter = 2000;

/* ================================================================
 * Physical device mapping (CUDA counterpart of the ROCm fix in
 * roctracer_callback.c, commit 1f5b889)
 *
 * cudaGetDevice()/cuptiGetDeviceId() report the device ordinal
 * RELATIVE TO this process's CUDA_VISIBLE_DEVICES-masked view, which
 * is always 0 under one-GPU-per-rank launchers — regardless of which
 * physical GPU the rank is bound to.  Verified on matrix (Slurm,
 * 4×H100): all ranks traced devId=0 while actually bound to physical
 * GPUs 1,0,3,2 (job 283220; eva/Castro/results/matrix/devid_findings.md).
 *
 * The relative ordinal is an index into the CUDA_VISIBLE_DEVICES
 * token list, so we parse the list once and map ordinal -> token.
 * Unlike the ROCm first-token-offset approach, an explicit map is
 * also correct for non-contiguous and reordered lists (Slurm binds
 * ranks to scrambled physical indices).
 *
 * Identity fallback (map empty) when the list cannot be trusted:
 *   - variable unset/empty (no masking),
 *   - UUID/MIG tokens ("GPU-...", "MIG-..."),
 *   - malformed tokens,
 *   - cgroup-constrained jobs that renumber devices (Slurm
 *     --gpus-per-task): the env var names physical GPUs that are not
 *     the cgroup's renumbered ordinals — but in that configuration
 *     CUDA either fails to init (verified: CUDA error 100) or the
 *     env var is "0", so identity is the best available answer.
 * ================================================================ */
#define CUDA_MAX_VISIBLE_DEVICES 64
static int cuda_visible_map[CUDA_MAX_VISIBLE_DEVICES];
static int cuda_visible_count = 0; /* 0 = identity mapping */

/* Called once from pinsight_cuda_init(), before any callback fires. */
static void cuda_parse_visible_devices(void) {
  cuda_visible_count = 0;
  const char *v = getenv("CUDA_VISIBLE_DEVICES");
  if (!v || !*v)
    return;
  const char *p = v;
  int n = 0;
  while (n < CUDA_MAX_VISIBLE_DEVICES) {
    char *end;
    long d = strtol(p, &end, 10);
    if (end == p || d < 0 || (*end != ',' && *end != '\0'))
      return; /* non-integer or malformed token: keep identity */
    cuda_visible_map[n++] = (int)d;
    if (*end == '\0')
      break;
    p = end + 1;
  }
  cuda_visible_count = n;
}

/* Map a process-relative device ordinal to the physical device index.
 * Ordinals beyond the parsed list pass through unchanged. */
static inline unsigned int cuda_physical_devId(unsigned int devId) {
  return (devId < (unsigned int)cuda_visible_count)
             ? (unsigned int)cuda_visible_map[devId]
             : devId;
}

/* ================================================================
 * Per-thread CUDA context/device cache (Level 2 optimization)
 *
 * In multi-GPU MPI apps like Castro, each rank uses a single GPU.
 * cuptiGetContextId/DeviceId cost ~200ns each per call, adding up
 * to ~0.5s over 1M CUPTI callbacks.  Cache the mapping in TLS so
 * the expensive CUPTI queries are done only once per thread.
 * The cached devId is the PHYSICAL device index (mapped above).
 * ================================================================ */
static __thread int cuda_tls_cached = 0;
static __thread unsigned int cuda_tls_cxtId = 0;
static __thread unsigned int cuda_tls_devId = 0;
static __thread CUcontext cuda_tls_context = NULL;

static inline void cuda_cache_context_device(CUcontext ctx, unsigned int *cxtId,
                                             unsigned int *devId) {
  if (__builtin_expect(cuda_tls_cached && cuda_tls_context == ctx, 1)) {
    *cxtId = cuda_tls_cxtId;
    *devId = cuda_tls_devId;
    return;
  }
  cuptiGetContextId(ctx, cxtId);
  unsigned int relDevId;
  cuptiGetDeviceId(ctx, &relDevId);
  *devId = cuda_physical_devId(relDevId);
  cuda_tls_cxtId = *cxtId;
  cuda_tls_devId = *devId;
  cuda_tls_context = ctx;
  cuda_tls_cached = 1;
}

/* ================================================================
 * Fast timestamp (Level 2 optimization)
 *
 * cuptiGetTimestamp() costs ~300ns (CUPTI driver call).
 * clock_gettime(CLOCK_MONOTONIC) costs ~25ns (vDSO, no syscall).
 * We record the offset between the two clocks once during clock
 * calibration, then use clock_gettime + offset for all subsequent
 * timestamps.  The clocks are both monotonic and tick at the same
 * rate, so the offset is constant for the process lifetime.
 * ================================================================ */
static int64_t cupti_clock_offset_ns = 0; /* cupti_ts - monotonic_ns */
static int     clock_calibration_done = 0; /* plain int for __atomic builtins */

static inline uint64_t cuda_fast_timestamp(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  uint64_t monotonic_ns =
      (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
  return (uint64_t)((int64_t)monotonic_ns + cupti_clock_offset_ns);
}

/**
 * Ensure pinsight_thread_data is initialized for the calling thread.
 */
static inline void cuda_ensure_thread_init(void) {
  if (!pinsight_thread_data.initialized) {
    int tid = __atomic_fetch_add(&cuda_thread_id_counter, 1, __ATOMIC_RELAXED);
    init_thread_data(tid);
  }
}

/* ================================================================
 * CUPTI subscriber and mode management
 * ================================================================ */

static CUpti_SubscriberHandle subscriber;

/* Forward declarations for activity buffer callbacks (defined below) */
static void CUPTIAPI activity_bufferRequested(uint8_t **buffer, size_t *size,
                                              size_t *maxNumRecords);
static void CUPTIAPI activity_bufferCompleted(CUcontext ctx, uint32_t streamId,
                                              uint8_t *buffer, size_t size,
                                              size_t validSize);

static _Atomic int activity_registered = 0;
static _Atomic int activity_init_done = 0;

/* Gate for emitting activity records in activity_bufferCompleted().  NOT the
 * live domain mode: CUPTI delivers buffers asynchronously (on buffer-full or at
 * a flush), so the mode at delivery time may differ from the mode when the
 * records were collected.  Instead this flag tracks "the records currently being
 * collected should be emitted", and pinsight_control_cuda_apply_mode() flushes
 * (blocking) at every MONITORING<->TRACING boundary *before* clearing it — so a
 * batch collected under TRACING is always emitted, and is never stranded for a
 * later MONITORING delivery to drop.  Set/cleared only by the control thread
 * (and the first-callback init); read in activity_bufferCompleted(). */
static volatile int cuda_activity_emit = 0;

/* Register the activity buffer callbacks exactly once.  This only installs
 * function pointers (no CUDA context required), so it is safe from any thread. */
static void cupti_activity_register_once(void) {
  if (!activity_registered &&
      __atomic_exchange_n(&activity_registered, 1, __ATOMIC_SEQ_CST) == 0) {
    cuptiActivityRegisterCallbacks(activity_bufferRequested,
                                   activity_bufferCompleted);
  }
}

/* Start activity COLLECTION (kernel + memcpy).  cuptiActivityEnable requires the
 * CUDA driver to be initialized, so this must run from a context where CUDA is
 * live (a CUPTI callback, or the control thread while the app is actively using
 * CUDA).  Registration is ensured first. */
static void cupti_activity_enable_collection(void) {
  cupti_activity_register_once();
  cuptiActivityEnable(CUPTI_ACTIVITY_KIND_MEMCPY);
  cuptiActivityEnable(CUPTI_ACTIVITY_KIND_KERNEL);
  cuptiActivityEnable(CUPTI_ACTIVITY_KIND_CONCURRENT_KERNEL);
}

/* Stop activity COLLECTION so MONITORING/STANDBY/OFF do not keep filling buffers
 * and firing bufferCompleted for records that are never emitted. */
static void cupti_activity_disable_collection(void) {
  cuptiActivityDisable(CUPTI_ACTIVITY_KIND_MEMCPY);
  cuptiActivityDisable(CUPTI_ACTIVITY_KIND_KERNEL);
  cuptiActivityDisable(CUPTI_ACTIVITY_KIND_CONCURRENT_KERNEL);
}

/* Deferred one-time activity setup, run from the first CUPTI callback (when a
 * CUDA context exists).  Registers the buffer callbacks always; starts
 * COLLECTION only if the starting mode is TRACING — MONITORING registers but
 * does not collect, otherwise it would fill buffers and fire bufferCompleted for
 * records that are dropped by the emit gate (pure overhead).  Runtime
 * MONITORING<->TRACING transitions are handled by the control thread in
 * pinsight_control_cuda_apply_mode(). */
static void cupti_activity_init_once(void) {
  if (activity_init_done ||
      __atomic_exchange_n(&activity_init_done, 1, __ATOMIC_SEQ_CST) != 0)
    return;
  cupti_activity_register_once();
  if (domain_default_trace_config[CUDA_domain_index].mode ==
      PINSIGHT_DOMAIN_TRACING) {
    cupti_activity_enable_collection();
    cuda_activity_emit = 1;
  }
}

/* Compute the CLOCK_MONOTONIC -> cuptiGetTimestamp offset and emit the
 * calibration anchor, exactly once, on the first callback.  Records both clocks
 * at the same instant so analysis tools can align GPU activity records with CPU
 * events.
 *
 * This MUST be deferred to a callback, not done at init: emitting a tracepoint
 * from a library constructor is unreliable (the LTTng provider may not be
 * registered yet) and cuptiGetTimestamp may not be valid before the CUDA
 * runtime initializes.  Both are ready by the first callback.  (Verified on the
 * HIP side that the analogous init-time approach drops the anchor and yields a
 * bogus offset.) */
static inline void cuda_calibrate_once(void) {
  /* Fast path: plain read, predicted taken once calibrated — no atomic cost.
   * A racing thread that still sees 0 is caught by the atomic exchange below,
   * so at most one thread ever does the calibration. */
  if (__builtin_expect(clock_calibration_done, 1))
    return;
  if (__atomic_exchange_n(&clock_calibration_done, 1, __ATOMIC_SEQ_CST) == 0) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    uint64_t monotonic_ns =
        (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
    uint64_t cupti_ts;
    cuptiGetTimestamp(&cupti_ts);
    cupti_clock_offset_ns = (int64_t)cupti_ts - (int64_t)monotonic_ns;
    lttng_ust_tracepoint(cupti_pinsight_lttng_ust, cuda_clock_calibration,
                         monotonic_ns, cupti_ts);
  }
}

/* ================================================================
 * Main CUPTI callback — entry point for all runtime API events.
 *
 * Structure:
 *   1. Deferred reconfig handler (SIGUSR1, mode_change_requested)
 *   2. Domain mode check (OFF → return)
 *   3. Only handle CUPTI_CB_DOMAIN_RUNTIME_API
 *   4. Ensure thread initialization
 *   5. Extract common callback info (context, device, correlation)
 *   6. Per-cbid dispatch: lexgion begin/end + rate + trace
 * ================================================================ */

void CUPTIAPI CUPTI_callback_lttng(void *userdata, CUpti_CallbackDomain domain,
                                   CUpti_CallbackId cbid,
                                   const CUpti_CallbackData *cbInfo) {
  /* ----------------------------------------------------------
   * 0. Guard against internal CUDA init callbacks.
   *    CUPTI 12+ fires callbacks with NULL or invalid cbInfo during CUDA
   *    module/kernel registration at startup (before any user CUDA call).
   *    These have no valid callbackSite, context, or function name.
   *    Skip them immediately to prevent crashes in multi-GPU MPI runs.
   * ---------------------------------------------------------- */
  if (!cbInfo || !cbInfo->context)
    return;

  /* ----------------------------------------------------------
   * 1. Check for app pause (introspection support)
   * ---------------------------------------------------------- */
  pinsight_check_pause();

  /* ----------------------------------------------------------
   * 2. Domain mode check — OFF → skip everything
   *    Reads volatile field — control thread updates it.
   * ---------------------------------------------------------- */
  if (!PINSIGHT_DOMAIN_ACTIVE(
          domain_default_trace_config[CUDA_domain_index].mode))
    return;

  /* 3. Only runtime API domain */
  if (domain != CUPTI_CB_DOMAIN_RUNTIME_API)
    return;

  /* 4. Ensure thread data initialized + deferred Activity API init +
   *    one-shot clock calibration (first callback only).
   *    cupti_activity_init_once() is safe here: a CUDA context exists
   *    (this is a CUDA API callback), making cuptiActivityEnable valid. */
  cuda_ensure_thread_init();
  cupti_activity_init_once();
  cuda_calibrate_once();

  /* ----------------------------------------------------------
   * 5. Extract common callback info
   *    Level 2: cached context/device IDs avoid 2 CUPTI calls/event
   * ---------------------------------------------------------- */
  const CUcontext context = cbInfo->context;
  unsigned int cxtId, devId;
  cuda_cache_context_device(context, &cxtId, &devId);
  unsigned int correlationId = cbInfo->correlationId;

  /* ----------------------------------------------------------
   * 6. Per-cbid dispatch
   * ---------------------------------------------------------- */

  /* ========== Kernel Launch ========== */
  if (cbid == CUPTI_RUNTIME_TRACE_CBID_cudaLaunch_v3020 ||
      cbid == CUPTI_RUNTIME_TRACE_CBID_cudaLaunchKernel_v7000) {

    /* Skip internal CUDA module registration callbacks that CUPTI
     * may fire with a kernel-launch cbid during CUDA init.
     * These have no valid symbolName or functionName. */
    if (!cbInfo->symbolName && !cbInfo->functionName)
      return;

    /* Use the kernel symbol name pointer as the lexgion code pointer.
     * Each unique kernel name maps to a unique lexgion, which is the
     * desired behavior for tracking per-kernel statistics.
     *
     * NOTE: __builtin_return_address(N) for large N (e.g., 11) is
     * UNSAFE in CUPTI callbacks — the call stack depth varies with
     * CUDA driver version and context (module registration vs actual
     * kernel launch).  Walking too deep segfaults.  Using symbolName
     * is robust, always valid, and gives better lexgion granularity. */
    const void *codeptr = (const void *)cbInfo->symbolName;
    const char *kernelName = cbInfo->symbolName;

    if (cbInfo->callbackSite == CUPTI_API_ENTER) {
      lexgion_record_t *record =
          lexgion_begin(CUDA_LEXGION, CUDA_EVENT_KERNEL_LAUNCH, codeptr);
      lexgion_t *lgp = record->lgp;

      /* Name resolution: cbInfo->symbolName is the kernel name (e.g. "vectorAdd").
       * CUPTI-owned, stable for the session lifetime — zero allocation. */
      if (lgp->name_resolved_gen != trace_config_change_counter) {
        lgp->name = cbInfo->symbolName;
        lgp->filename_hint = NULL;
        lgp->name_resolved_gen = trace_config_change_counter;
        lgp->trace_config_change_counter = (unsigned int)-1;
      }

      /* MONITOR mode: skip config resolution + rate decision.
       * Only lexgion_begin above (LRU + count) runs. */
      if (PINSIGHT_SHOULD_TRACE(CUDA_domain_index)) {
        lexgion_set_top_trace_bit_domain_event(lgp, CUDA_domain_index,
                                               CUDA_EVENT_KERNEL_LAUNCH);
      }

      if (PINSIGHT_SHOULD_TRACE(CUDA_domain_index) && lgp->trace_bit) {
        cudaLaunchKernel_v7000_params *p =
            (cudaLaunchKernel_v7000_params *)cbInfo->functionParams;
        cudaStream_t stream = p->stream;
        unsigned int streamId;
        cuptiGetStreamIdEx(context, stream, 0, &streamId);
        struct contextStreamId_t ctxStreamId;
        ctxStreamId.contextId = cxtId;
        ctxStreamId.streamId = streamId;
        uint64_t timeStamp = cuda_fast_timestamp();
        dim3 grid = p->gridDim;
        dim3 block = p->blockDim;
        struct dimension_t dim;
        dim.gridx = grid.x;
        dim.gridy = grid.y;
        dim.gridz = grid.z;
        dim.blockx = block.x;
        dim.blocky = block.y;
        dim.blockz = block.z;
#ifdef PINSIGHT_BACKTRACE
        retrieve_backtrace();
#endif
        lttng_ust_tracepoint(cupti_pinsight_lttng_ust, cudaKernelLaunch_begin,
                             devId, correlationId, timeStamp, codeptr,
                             kernelName, &ctxStreamId, &dim);
      }
    } else if (cbInfo->callbackSite == CUPTI_API_EXIT) {
      lexgion_t *lgp = lexgion_end(NULL);
      if (lgp && PINSIGHT_SHOULD_TRACE(CUDA_domain_index) && lgp->trace_bit) {
        uint64_t timeStamp = cuda_fast_timestamp();
        lttng_ust_tracepoint(cupti_pinsight_lttng_ust, cudaKernelLaunch_end,
                             devId, correlationId, timeStamp, codeptr,
                             kernelName);
        lexgion_post_trace_update(lgp);
      }
    }
    return;
  }

  /* ========== cudaMemcpy (synchronous) ========== */
  if (cbid == CUPTI_RUNTIME_TRACE_CBID_cudaMemcpy_v3020) {
    const void *codeptr = (const void *)cbInfo->functionName;
    cudaMemcpy_v3020_params *funcParams =
        (cudaMemcpy_v3020_params *)cbInfo->functionParams;
    int kind = funcParams->kind;
    int event_id = cuda_memcpy_event_id(kind);

    if (cbInfo->callbackSite == CUPTI_API_ENTER) {
      lexgion_record_t *record = lexgion_begin(CUDA_LEXGION, event_id, codeptr);
      lexgion_t *lgp = record->lgp;

      /* Name resolution: use CUDA event name from domain_info_table for
       * non-kernel API calls (e.g. "CUDA_cudaMemcpy"). Zero allocation. */
      if (lgp->name_resolved_gen != trace_config_change_counter) {
        lgp->name = domain_info_table[CUDA_domain_index].event_table[event_id].name;
        lgp->filename_hint = NULL;
        lgp->name_resolved_gen = trace_config_change_counter;
        lgp->trace_config_change_counter = (unsigned int)-1;
      }

      if (PINSIGHT_SHOULD_TRACE(CUDA_domain_index)) {
        lexgion_set_top_trace_bit_domain_event(lgp, CUDA_domain_index,
                                               event_id);
      }

      if (PINSIGHT_SHOULD_TRACE(CUDA_domain_index) && lgp->trace_bit) {
        const char *funName = cbInfo->functionName;
        void *dst = funcParams->dst;
        const void *src = funcParams->src;
        unsigned int count = funcParams->count;
        uint64_t timeStamp = cuda_fast_timestamp();
#ifdef PINSIGHT_BACKTRACE
        retrieve_backtrace();
#endif
        lttng_ust_tracepoint(cupti_pinsight_lttng_ust, cudaMemcpy_begin, devId,
                             correlationId, timeStamp, codeptr, funName, dst,
                             src, count, kind);
      }
    } else if (cbInfo->callbackSite == CUPTI_API_EXIT) {
      lexgion_t *lgp = lexgion_end(NULL);
      if (lgp && PINSIGHT_SHOULD_TRACE(CUDA_domain_index) && lgp->trace_bit) {
        int return_val = *((int *)cbInfo->functionReturnValue);
        const char *funName = cbInfo->functionName;
        uint64_t timeStamp = cuda_fast_timestamp();
        lttng_ust_tracepoint(cupti_pinsight_lttng_ust, cudaMemcpy_end, devId,
                             correlationId, timeStamp, codeptr, funName,
                             return_val);
        lexgion_post_trace_update(lgp);
      }
    }
    return;
  }

  /* ========== cudaMemcpyAsync ========== */
  if (cbid == CUPTI_RUNTIME_TRACE_CBID_cudaMemcpyAsync_v3020) {
    const void *codeptr = (const void *)cbInfo->functionName;
    cudaMemcpyAsync_v3020_params *p =
        (cudaMemcpyAsync_v3020_params *)cbInfo->functionParams;

    if (cbInfo->callbackSite == CUPTI_API_ENTER) {
      int kind = p->kind;
      int event_id = CUDA_EVENT_MEMCPY_ASYNC;

      lexgion_record_t *record = lexgion_begin(CUDA_LEXGION, event_id, codeptr);
      lexgion_t *lgp = record->lgp;

      if (lgp->name_resolved_gen != trace_config_change_counter) {
        lgp->name = domain_info_table[CUDA_domain_index].event_table[event_id].name;
        lgp->filename_hint = NULL;
        lgp->name_resolved_gen = trace_config_change_counter;
        lgp->trace_config_change_counter = (unsigned int)-1;
      }

      if (PINSIGHT_SHOULD_TRACE(CUDA_domain_index)) {
        lexgion_set_top_trace_bit_domain_event(lgp, CUDA_domain_index,
                                               event_id);
      }

      if (PINSIGHT_SHOULD_TRACE(CUDA_domain_index) && lgp->trace_bit) {
        const char *funName = cbInfo->functionName;
        void *dst = p->dst;
        const void *src = p->src;
        unsigned int count = p->count;
        cudaStream_t stream = p->stream;
        unsigned int streamId;
        cuptiGetStreamIdEx(context, stream, 0, &streamId);
        struct contextStreamId_t ctxStreamId;
        ctxStreamId.contextId = cxtId;
        ctxStreamId.streamId = streamId;
        uint64_t timeStamp = cuda_fast_timestamp();
#ifdef PINSIGHT_BACKTRACE
        retrieve_backtrace();
#endif
        lttng_ust_tracepoint(cupti_pinsight_lttng_ust, cudaMemcpyAsync_begin,
                             devId, correlationId, timeStamp, codeptr, funName,
                             dst, src, count, kind, &ctxStreamId);
      }
    } else if (cbInfo->callbackSite == CUPTI_API_EXIT) {
      lexgion_t *lgp = lexgion_end(NULL);
      if (lgp && PINSIGHT_SHOULD_TRACE(CUDA_domain_index) && lgp->trace_bit) {
        int return_val = *((int *)cbInfo->functionReturnValue);
        const char *funName = cbInfo->functionName;
        uint64_t timeStamp = cuda_fast_timestamp();
        lttng_ust_tracepoint(cupti_pinsight_lttng_ust, cudaMemcpyAsync_end,
                             devId, correlationId, timeStamp, codeptr, funName,
                             return_val);
        lexgion_post_trace_update(lgp);
      }
    }
    return;
  }

  /* ========== cudaMalloc ========== */
  if (cbid == CUPTI_RUNTIME_TRACE_CBID_cudaMalloc_v3020) {
    const void *codeptr = (const void *)cbInfo->functionName;
    const char *funName = cbInfo->functionName;
    cudaMalloc_v3020_params *p =
        (cudaMalloc_v3020_params *)cbInfo->functionParams;

    if (cbInfo->callbackSite == CUPTI_API_ENTER) {
      lexgion_record_t *record =
          lexgion_begin(CUDA_LEXGION, CUDA_EVENT_MALLOC, codeptr);
      lexgion_t *lgp = record->lgp;

      if (lgp->name_resolved_gen != trace_config_change_counter) {
        lgp->name = domain_info_table[CUDA_domain_index]
                        .event_table[CUDA_EVENT_MALLOC].name;
        lgp->filename_hint = NULL;
        lgp->name_resolved_gen = trace_config_change_counter;
        lgp->trace_config_change_counter = (unsigned int)-1;
      }

      if (PINSIGHT_SHOULD_TRACE(CUDA_domain_index)) {
        lexgion_set_top_trace_bit_domain_event(lgp, CUDA_domain_index,
                                               CUDA_EVENT_MALLOC);
      }

      if (PINSIGHT_SHOULD_TRACE(CUDA_domain_index) && lgp->trace_bit) {
        size_t size = p->size;
        uint64_t timeStamp = cuda_fast_timestamp();
#ifdef PINSIGHT_BACKTRACE
        retrieve_backtrace();
#endif
        lttng_ust_tracepoint(cupti_pinsight_lttng_ust, cudaMalloc_begin, devId,
                             correlationId, timeStamp, codeptr, funName, size);
      }
    } else if (cbInfo->callbackSite == CUPTI_API_EXIT) {
      lexgion_t *lgp = lexgion_end(NULL);
      if (lgp && PINSIGHT_SHOULD_TRACE(CUDA_domain_index) && lgp->trace_bit) {
        /* At EXIT the device address has been written into *devPtr */
        void *dev_ptr = p->devPtr ? *(p->devPtr) : NULL;
        int return_val = *((int *)cbInfo->functionReturnValue);
        uint64_t timeStamp = cuda_fast_timestamp();
        lttng_ust_tracepoint(cupti_pinsight_lttng_ust, cudaMalloc_end, devId,
                             correlationId, timeStamp, codeptr, funName, dev_ptr,
                             return_val);
        lexgion_post_trace_update(lgp);
      }
    }
    return;
  }

  /* ========== cudaFree ========== */
  if (cbid == CUPTI_RUNTIME_TRACE_CBID_cudaFree_v3020) {
    const void *codeptr = (const void *)cbInfo->functionName;
    const char *funName = cbInfo->functionName;
    cudaFree_v3020_params *p = (cudaFree_v3020_params *)cbInfo->functionParams;

    if (cbInfo->callbackSite == CUPTI_API_ENTER) {
      lexgion_record_t *record =
          lexgion_begin(CUDA_LEXGION, CUDA_EVENT_FREE, codeptr);
      lexgion_t *lgp = record->lgp;

      if (lgp->name_resolved_gen != trace_config_change_counter) {
        lgp->name = domain_info_table[CUDA_domain_index]
                        .event_table[CUDA_EVENT_FREE].name;
        lgp->filename_hint = NULL;
        lgp->name_resolved_gen = trace_config_change_counter;
        lgp->trace_config_change_counter = (unsigned int)-1;
      }

      if (PINSIGHT_SHOULD_TRACE(CUDA_domain_index)) {
        lexgion_set_top_trace_bit_domain_event(lgp, CUDA_domain_index,
                                               CUDA_EVENT_FREE);
      }

      if (PINSIGHT_SHOULD_TRACE(CUDA_domain_index) && lgp->trace_bit) {
        void *dev_ptr = p->devPtr;
        uint64_t timeStamp = cuda_fast_timestamp();
#ifdef PINSIGHT_BACKTRACE
        retrieve_backtrace();
#endif
        lttng_ust_tracepoint(cupti_pinsight_lttng_ust, cudaFree_begin, devId,
                             correlationId, timeStamp, codeptr, funName,
                             dev_ptr);
      }
    } else if (cbInfo->callbackSite == CUPTI_API_EXIT) {
      lexgion_t *lgp = lexgion_end(NULL);
      if (lgp && PINSIGHT_SHOULD_TRACE(CUDA_domain_index) && lgp->trace_bit) {
        int return_val = *((int *)cbInfo->functionReturnValue);
        uint64_t timeStamp = cuda_fast_timestamp();
        lttng_ust_tracepoint(cupti_pinsight_lttng_ust, cudaFree_end, devId,
                             correlationId, timeStamp, codeptr, funName,
                             return_val);
        lexgion_post_trace_update(lgp);
      }
    }
    return;
  }

  /* ========== cudaDeviceSynchronize ========== */
  if (cbid == CUPTI_RUNTIME_TRACE_CBID_cudaDeviceSynchronize_v3020) {
    const void *codeptr = (const void *)cbInfo->functionName;
    const char *funName = cbInfo->functionName;

    if (cbInfo->callbackSite == CUPTI_API_ENTER) {
      lexgion_record_t *record =
          lexgion_begin(CUDA_LEXGION, CUDA_EVENT_DEVICE_SYNCHRONIZE, codeptr);
      lexgion_t *lgp = record->lgp;

      if (lgp->name_resolved_gen != trace_config_change_counter) {
        lgp->name = domain_info_table[CUDA_domain_index]
                        .event_table[CUDA_EVENT_DEVICE_SYNCHRONIZE].name;
        lgp->filename_hint = NULL;
        lgp->name_resolved_gen = trace_config_change_counter;
        lgp->trace_config_change_counter = (unsigned int)-1;
      }

      if (PINSIGHT_SHOULD_TRACE(CUDA_domain_index)) {
        lexgion_set_top_trace_bit_domain_event(lgp, CUDA_domain_index,
                                               CUDA_EVENT_DEVICE_SYNCHRONIZE);
      }

      if (PINSIGHT_SHOULD_TRACE(CUDA_domain_index) && lgp->trace_bit) {
        uint64_t timeStamp = cuda_fast_timestamp();
#ifdef PINSIGHT_BACKTRACE
        retrieve_backtrace();
#endif
        lttng_ust_tracepoint(cupti_pinsight_lttng_ust, cudaDeviceSync_begin,
                             devId, correlationId, timeStamp, codeptr, funName);
      }
    } else if (cbInfo->callbackSite == CUPTI_API_EXIT) {
      lexgion_t *lgp = lexgion_end(NULL);
      if (lgp && PINSIGHT_SHOULD_TRACE(CUDA_domain_index) && lgp->trace_bit) {
        int return_val = *((int *)cbInfo->functionReturnValue);
        uint64_t timeStamp = cuda_fast_timestamp();
        lttng_ust_tracepoint(cupti_pinsight_lttng_ust, cudaDeviceSync_end,
                             devId, correlationId, timeStamp, codeptr, funName,
                             return_val);
        lexgion_post_trace_update(lgp);
      }
    }
    return;
  }

  /* ========== cudaStreamSynchronize ========== */
  if (cbid == CUPTI_RUNTIME_TRACE_CBID_cudaStreamSynchronize_v3020) {
    const void *codeptr = (const void *)cbInfo->functionName;
    const char *funName = cbInfo->functionName;

    if (cbInfo->callbackSite == CUPTI_API_ENTER) {
      cudaStreamSynchronize_v3020_params *p =
          (cudaStreamSynchronize_v3020_params *)cbInfo->functionParams;
      cudaStream_t stream = p->stream;
      unsigned int streamId;
      cuptiGetStreamIdEx(context, stream, 0, &streamId);
      struct contextStreamId_t ctxStreamId;
      ctxStreamId.contextId = cxtId;
      ctxStreamId.streamId = streamId;

      lexgion_record_t *record =
          lexgion_begin(CUDA_LEXGION, CUDA_EVENT_STREAM_SYNCHRONIZE, codeptr);
      lexgion_t *lgp = record->lgp;

      if (lgp->name_resolved_gen != trace_config_change_counter) {
        lgp->name = domain_info_table[CUDA_domain_index]
                        .event_table[CUDA_EVENT_STREAM_SYNCHRONIZE].name;
        lgp->filename_hint = NULL;
        lgp->name_resolved_gen = trace_config_change_counter;
        lgp->trace_config_change_counter = (unsigned int)-1;
      }

      if (PINSIGHT_SHOULD_TRACE(CUDA_domain_index)) {
        lexgion_set_top_trace_bit_domain_event(lgp, CUDA_domain_index,
                                               CUDA_EVENT_STREAM_SYNCHRONIZE);
      }

      if (PINSIGHT_SHOULD_TRACE(CUDA_domain_index) && lgp->trace_bit) {
        uint64_t timeStamp = cuda_fast_timestamp();
#ifdef PINSIGHT_BACKTRACE
        retrieve_backtrace();
#endif
        lttng_ust_tracepoint(cupti_pinsight_lttng_ust, cudaStreamSync_begin,
                             devId, correlationId, timeStamp, codeptr, funName,
                             &ctxStreamId);
      }
    } else if (cbInfo->callbackSite == CUPTI_API_EXIT) {
      lexgion_t *lgp = lexgion_end(NULL);
      if (lgp && PINSIGHT_SHOULD_TRACE(CUDA_domain_index) && lgp->trace_bit) {
        int return_val = *((int *)cbInfo->functionReturnValue);
        uint64_t timeStamp = cuda_fast_timestamp();
        lttng_ust_tracepoint(cupti_pinsight_lttng_ust, cudaStreamSync_end,
                             devId, correlationId, timeStamp, codeptr, funName,
                             return_val);
        lexgion_post_trace_update(lgp);
      }
    }
    return;
  }

  /* cudaDeviceReset — flush all pending activity records before the
   * CUDA context is destroyed.  The blocking flag (1) waits for all
   * bufferCompleted callbacks to finish, ensuring GPU-side timing
   * records reach LTTng while the session is still active. */
  if (cbid == CUPTI_RUNTIME_TRACE_CBID_cudaDeviceReset_v3020) {
    if (cbInfo->callbackSite == CUPTI_API_ENTER) {
      cuptiActivityFlushAll(1); /* 1 = blocking */
    }
    return;
  }
}

/* ================================================================
 * Activity API — captures GPU-side timestamps for async operations.
 *
 * Design rationale (why both Callback and Activity APIs are needed):
 *   Callback API: provides codeptr (call-site) for lexgion identity,
 *                 rate control hooks, synchronous op timing.
 *   Activity API: provides actual GPU execution start/end timestamps
 *                 for async ops (cudaMemcpyAsync, kernel launch).
 *   They are linked by correlationId.
 *
 * CUDA 13 record types:
 *   CUpti_ActivityMemcpy6  — CUPTI_ACTIVITY_KIND_MEMCPY
 *   CUpti_ActivityKernel10 — CUPTI_ACTIVITY_KIND_KERNEL /
 *                            CUPTI_ACTIVITY_KIND_CONCURRENT_KERNEL
 * ================================================================ */

#define CUPTI_ACTIVITY_BUFFER_SIZE (8 * 1024 * 1024) /* 8 MB per buffer */
#define CUPTI_ACTIVITY_BUFFER_ALIGN 8

static void CUPTIAPI activity_bufferRequested(uint8_t **buffer, size_t *size,
                                              size_t *maxNumRecords) {
  *maxNumRecords = 0; /* unlimited */
  *size = CUPTI_ACTIVITY_BUFFER_SIZE;
  if (posix_memalign((void **)buffer, CUPTI_ACTIVITY_BUFFER_ALIGN,
                     CUPTI_ACTIVITY_BUFFER_SIZE) != 0)
    *buffer = NULL;
}

static void CUPTIAPI activity_bufferCompleted(CUcontext ctx, uint32_t streamId,
                                              uint8_t *buffer, size_t size,
                                              size_t validSize) {
  if (!buffer)
    return;
  /* Suppress in OFF/STANDBY/MONITORING.  Unlike the ROCTracer pool (which owns
   * its buffer), CUPTI hands us a buffer we allocated in activity_bufferRequested
   * — so we MUST free() it even when skipping, or leak 8 MB per buffer.  Only
   * async ops still in-flight at a TRACING->lower switch can leak/drop —
   * best-effort by design. */
  if (!cuda_activity_emit) {
    free(buffer);
    return;
  }
  CUpti_Activity *record = NULL;

  do {
    CUptiResult status = cuptiActivityGetNextRecord(buffer, validSize, &record);
    if (status != CUPTI_SUCCESS)
      break;

    switch (record->kind) {

    case CUPTI_ACTIVITY_KIND_MEMCPY: {
      /* GPU-side timing for both cudaMemcpy and cudaMemcpyAsync.
       * For synchronous cudaMemcpy, this duplicates the callback
       * tracepoint but confirms GPU-side timing — harmless.
       * For cudaMemcpyAsync, this is the only source of actual
       * transfer time. */
      CUpti_ActivityMemcpy5 *m = (CUpti_ActivityMemcpy5 *)record;
      /* Activity deviceId is the same process-relative ordinal as the
       * callback path (CUPTI numbers devices within this process's
       * visible set), so the same physical mapping applies. */
      lttng_ust_tracepoint(cupti_pinsight_lttng_ust, cudaMemcpyActivity,
                           cuda_physical_devId(m->deviceId), m->correlationId,
                           m->start, m->end, m->bytes, (int)m->copyKind,
                           m->contextId, m->streamId);
      break;
    }

    case CUPTI_ACTIVITY_KIND_KERNEL:
    case CUPTI_ACTIVITY_KIND_CONCURRENT_KERNEL: {
      /* Actual GPU execution window — not just CPU-side launch time. */
      CUpti_ActivityKernel9 *k = (CUpti_ActivityKernel9 *)record;
      lttng_ust_tracepoint(cupti_pinsight_lttng_ust, cudaKernelActivity,
                           cuda_physical_devId(k->deviceId), k->correlationId,
                           k->start, k->end, k->contextId, k->streamId,
                           k->name ? k->name : "");
      break;
    }

    default:
      break;
    }
  } while (1);

  free(buffer);
}

/* ================================================================
 * Initialization and finalization
 * ================================================================ */

/* ================================================================
 * Helper: enable/disable all CUPTI callbacks.
 * Called by control thread and LTTNG_CUPTI_Init.
 * cuptiEnableCallback is process-global and thread-safe.
 * ================================================================ */
static void cupti_set_all_callbacks(int enable) {
  /* Only enable the cbids the callback actually handles.  Deprecated/no-op
   * cbids (cudaConfigureCall, cudaThreadSynchronize, cudaStreamCreate,
   * cudaThreadExit) are intentionally left disabled so they never fire. */
  cuptiEnableCallback(enable, subscriber, CUPTI_CB_DOMAIN_RUNTIME_API,
                      CUPTI_RUNTIME_TRACE_CBID_cudaLaunch_v3020);
  cuptiEnableCallback(enable, subscriber, CUPTI_CB_DOMAIN_RUNTIME_API,
                      CUPTI_RUNTIME_TRACE_CBID_cudaLaunchKernel_v7000);
  cuptiEnableCallback(enable, subscriber, CUPTI_CB_DOMAIN_RUNTIME_API,
                      CUPTI_RUNTIME_TRACE_CBID_cudaStreamSynchronize_v3020);
  cuptiEnableCallback(enable, subscriber, CUPTI_CB_DOMAIN_RUNTIME_API,
                      CUPTI_RUNTIME_TRACE_CBID_cudaDeviceSynchronize_v3020);
  cuptiEnableCallback(enable, subscriber, CUPTI_CB_DOMAIN_RUNTIME_API,
                      CUPTI_RUNTIME_TRACE_CBID_cudaMemcpy_v3020);
  cuptiEnableCallback(enable, subscriber, CUPTI_CB_DOMAIN_RUNTIME_API,
                      CUPTI_RUNTIME_TRACE_CBID_cudaMemcpyAsync_v3020);
  cuptiEnableCallback(enable, subscriber, CUPTI_CB_DOMAIN_RUNTIME_API,
                      CUPTI_RUNTIME_TRACE_CBID_cudaMalloc_v3020);
  cuptiEnableCallback(enable, subscriber, CUPTI_CB_DOMAIN_RUNTIME_API,
                      CUPTI_RUNTIME_TRACE_CBID_cudaFree_v3020);
  cuptiEnableCallback(enable, subscriber, CUPTI_CB_DOMAIN_RUNTIME_API,
                      CUPTI_RUNTIME_TRACE_CBID_cudaDeviceReset_v3020);
}

/**
 * Called by the control thread to apply CUDA domain mode changes.
 * 4-mode semantics:
 *   OFF:        cuptiUnsubscribe() — permanent teardown, irreversible.
 *   STANDBY:    cuptiEnableCallback(0) — dispatch off, subscriber alive.
 *   MONITORING: cuptiEnableCallback(1) — dispatch on, LRU+count only.
 *   TRACING:    cuptiEnableCallback(1) — dispatch on, full tracing.
 */
static int cuda_permanently_off = 0; /* Once set, never apply again */

/* Called once from LTTNG_CUPTI_Init before the control thread starts.
 * Subscribes and enables callbacks if the starting mode is active; skips
 * subscribe entirely if starting OFF (no overhead).
 * Sets last_mode = starting mode so apply_mode never sees NONE. */
static void pinsight_cuda_init(void) {
  pinsight_domain_mode_t mode =
      domain_default_trace_config[CUDA_domain_index].mode;

  /* Parse CUDA_VISIBLE_DEVICES once, before any callback can fire,
   * so cuda_physical_devId() is race-free (read-only afterwards). */
  cuda_parse_visible_devices();

  if (mode == PINSIGHT_DOMAIN_OFF) {
    cuda_permanently_off = 1;
    fprintf(stderr, "PInsight: CUDA domain starting OFF — not subscribing\n");
    domain_default_trace_config[CUDA_domain_index].last_mode = mode;
    return;
  }

  /* Subscribe once — safe before any CUDA context exists */
  cuptiSubscribe(&subscriber, (CUpti_CallbackFunc)CUPTI_callback_lttng, NULL);

  /* Clock calibration is deferred to the first callback (cuptiGetTimestamp and
   * the LTTng provider are not reliably ready at constructor time). */
  if (PINSIGHT_DOMAIN_ACTIVE(mode))
    cupti_set_all_callbacks(1);
  /* STANDBY: subscriber live, individual callbacks off */

  domain_default_trace_config[CUDA_domain_index].last_mode = mode;
}

/* Called by the control thread for runtime mode transitions.
 * Precondition: pinsight_cuda_init() has run, so last_mode is never NONE
 * and subscriber is always registered when we reach here (cuda_permanently_off
 * gate handles the OFF case). */
void pinsight_control_cuda_apply_mode(void) {
  if (cuda_permanently_off)
    return;

  pinsight_domain_mode_t mode =
      domain_default_trace_config[CUDA_domain_index].mode;
  pinsight_domain_mode_t last =
      domain_default_trace_config[CUDA_domain_index].last_mode;

  if (mode == last)
    return;

  /* --- Leaving TRACING (→ MONITORING/STANDBY/OFF): drain THEN stop collection. ---
   * Activity collection is enabled ONLY while TRACING, so MONITORING does not
   * keep filling buffers and firing bufferCompleted (which would be pure
   * overhead since nothing is emitted).  Flush first (blocking, so all in-flight
   * buffers are delivered) while cuda_activity_emit is still 1 — the in-flight
   * TRACING batch is emitted; then clear the gate and disable collection at the
   * source. */
  if (last == PINSIGHT_DOMAIN_TRACING && mode != PINSIGHT_DOMAIN_TRACING) {
    cuptiActivityFlushAll(1); /* emit==1 → emitted */
    cuda_activity_emit = 0;
    cupti_activity_disable_collection();
  }

  if (mode == PINSIGHT_DOMAIN_OFF) {
    if (PINSIGHT_DOMAIN_ACTIVE(last))
      cuptiActivityFlushAll(1);
    cuptiUnsubscribe(subscriber);
    cuda_permanently_off = 1;
    cuda_activity_emit = 0;
    fprintf(stderr, "PInsight: CUDA domain permanently OFF\n");
  } else if (mode == PINSIGHT_DOMAIN_STANDBY) {
    if (PINSIGHT_DOMAIN_ACTIVE(last))
      cupti_set_all_callbacks(0);
    /* Collection already stopped above for last==TRACING; for last==MONITORING
     * it was never on. */
  } else if (!PINSIGHT_DOMAIN_ACTIVE(last)) {
    /* STANDBY → MONITORING/TRACING: enable the API callbacks; start collection
     * only for TRACING. */
    cupti_set_all_callbacks(1);
    if (mode == PINSIGHT_DOMAIN_TRACING) {
      cupti_activity_enable_collection();
      cuda_activity_emit = 1;
    }
  } else if (mode == PINSIGHT_DOMAIN_TRACING) {
    /* MONITORING → TRACING: callbacks already on; (re)start collection. */
    cupti_activity_enable_collection();
    cuda_activity_emit = 1;
  }
  /* TRACING → MONITORING was fully handled by the leaving-TRACING block above. */
}

void LTTNG_CUPTI_Init(void) {
  pinsight_cuda_init();
  /* NOTE: Activity API (cuptiActivityEnable) requires an active CUDA
   * context and MUST NOT be called at library load time in multi-GPU
   * MPI runs where no context exists yet.  It is deferred to
   * cupti_activity_init_once(), called from the first CUPTI callback.
   * Clock calibration is likewise deferred to the first callback via
   * cuda_calibrate_once(). */
}

void LTTNG_CUPTI_Fini(void) {
  if (cuda_permanently_off)
    return; /* Already unsubscribed by OFF mode */
  /* Flush all pending activity records before unsubscribing.
   * Flag 1 = blocking: waits for all bufferCompleted callbacks to finish
   * before returning.  Critical for AMReX/Castro which does not call
   * cudaDeviceReset() — without this, GPU activity records may not be
   * delivered to LTTng before the session daemon stops.
   *
   * The final flush runs with cuda_activity_emit still reflecting the last mode,
   * so records collected while TRACING are emitted; if we ended in MONITORING/
   * STANDBY the gate frees buffers without emitting.  Collection is only on if
   * we ended in TRACING (emit==1) — disable it only then. */
  cuptiActivityFlushAll(1);
  if (cuda_activity_emit)
    cupti_activity_disable_collection();
  cuptiUnsubscribe(subscriber);
}
