//
// Created by Yonghong Yan on 12/12/19.
// Restructured for full trace control (domain modes, rate control,
// introspection) — April 2026.
//

#include <stdio.h>
#include <time.h>
#include <cupti.h>
#include <cupti_activity.h>
#include <cuda_runtime.h>
#include <cuda.h>
#include "pinsight.h"
#include "trace_config.h"
#include "pinsight_control_thread.h"

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
#define CUDA_EVENT_DEVICE_RESET        3
#define CUDA_EVENT_DEVICE_SYNCHRONIZE  4
#define CUDA_EVENT_STREAM_CREATE       5
#define CUDA_EVENT_STREAM_SYNCHRONIZE  7
#define CUDA_EVENT_MEMCPY_HTOD        12
#define CUDA_EVENT_MEMCPY_DTOH        13
#define CUDA_EVENT_MEMCPY_DTOD        14
#define CUDA_EVENT_MEMCPY_HTOH        15
#define CUDA_EVENT_MEMCPY_ASYNC       16
#define CUDA_EVENT_KERNEL_LAUNCH      23

/* ================================================================
 * Helper functions
 * ================================================================ */

int CUDA_get_device_id(void* arg) {
    int currentDevice;
    cudaGetDevice(&currentDevice);
    return currentDevice;
}

/**
 * Map cudaMemcpyKind to the corresponding CUDA domain event ID.
 */
static inline int cuda_memcpy_event_id(int kind) {
    switch (kind) {
        case cudaMemcpyHostToDevice:   return CUDA_EVENT_MEMCPY_HTOD;
        case cudaMemcpyDeviceToHost:   return CUDA_EVENT_MEMCPY_DTOH;
        case cudaMemcpyDeviceToDevice: return CUDA_EVENT_MEMCPY_DTOD;
        default:                       return CUDA_EVENT_MEMCPY_HTOH;
    }
}

/* Atomic counter for assigning unique IDs to non-OpenMP threads
 * (pure CUDA host threads, CUDA driver internal threads).
 * Starts at 2000 to avoid colliding with OpenMP thread IDs (0..N-1).
 * Each OS thread has its own TLS copy of pinsight_thread_data, so
 * the only shared state is this counter — hence the atomic. */
static _Atomic int cuda_thread_id_counter = 2000;

/* ================================================================
 * Per-thread CUDA context/device cache (Level 2 optimization)
 *
 * In multi-GPU MPI apps like Castro, each rank uses a single GPU.
 * cuptiGetContextId/DeviceId cost ~200ns each per call, adding up
 * to ~0.5s over 1M CUPTI callbacks.  Cache the mapping in TLS so
 * the expensive CUPTI queries are done only once per thread.
 * ================================================================ */
static __thread int      cuda_tls_cached = 0;
static __thread unsigned int cuda_tls_cxtId = 0;
static __thread unsigned int cuda_tls_devId = 0;
static __thread CUcontext    cuda_tls_context = NULL;

static inline void cuda_cache_context_device(CUcontext ctx,
                                              unsigned int *cxtId,
                                              unsigned int *devId) {
    if (__builtin_expect(cuda_tls_cached && cuda_tls_context == ctx, 1)) {
        *cxtId = cuda_tls_cxtId;
        *devId = cuda_tls_devId;
        return;
    }
    cuptiGetContextId(ctx, cxtId);
    cuptiGetDeviceId(ctx, devId);
    cuda_tls_cxtId   = *cxtId;
    cuda_tls_devId   = *devId;
    cuda_tls_context = ctx;
    cuda_tls_cached  = 1;
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
static int64_t cupti_clock_offset_ns = 0;  /* cupti_ts - monotonic_ns */
static _Atomic int fast_timestamp_ready = 0;

static inline uint64_t cuda_fast_timestamp(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    uint64_t monotonic_ns = (uint64_t)ts.tv_sec * 1000000000ULL
                          + (uint64_t)ts.tv_nsec;
    return (uint64_t)((int64_t)monotonic_ns + cupti_clock_offset_ns);
}

/**
 * Ensure pinsight_thread_data is initialized for the calling thread.
 */
static inline void cuda_ensure_thread_init(void) {
    if (!pinsight_thread_data.initialized) {
        int tid = __atomic_fetch_add(&cuda_thread_id_counter, 1,
                                     __ATOMIC_RELAXED);
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

static _Atomic int activity_init_done = 0;

static void cupti_activity_init_once(void) {
    if (!activity_init_done &&
        __atomic_exchange_n(&activity_init_done, 1, __ATOMIC_SEQ_CST) == 0) {
        cuptiActivityRegisterCallbacks(activity_bufferRequested,
                                       activity_bufferCompleted);
        cuptiActivityEnable(CUPTI_ACTIVITY_KIND_MEMCPY);
        cuptiActivityEnable(CUPTI_ACTIVITY_KIND_KERNEL);
        cuptiActivityEnable(CUPTI_ACTIVITY_KIND_CONCURRENT_KERNEL);
    }
}

/* Fired exactly once on the first CUDA API call, guaranteeing LTTng probes
 * are registered and the session is active.  Records both CLOCK_MONOTONIC
 * and cuptiGetTimestamp() at the same instant so analysis tools can compute
 * the constant clock offset and align GPU activity records with CPU events.
 * Also caches the offset for fast_timestamp use. */
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
        /* Cache offset for cuda_fast_timestamp() */
        cupti_clock_offset_ns = (int64_t)cupti_ts - (int64_t)monotonic_ns;
        __atomic_store_n(&fast_timestamp_ready, 1, __ATOMIC_RELEASE);
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

void CUPTIAPI CUPTI_callback_lttng(void *userdata,
                                   CUpti_CallbackDomain domain,
                                   CUpti_CallbackId cbid,
                                   const CUpti_CallbackData *cbInfo) {
    /* ----------------------------------------------------------
     * 0. Guard against internal CUDA init callbacks.
     *    CUPTI 12+ fires callbacks with NULL or invalid cbInfo during CUDA
     *    module/kernel registration at startup (before any user CUDA call).
     *    These have no valid callbackSite, context, or function name.
     *    Skip them immediately to prevent crashes in multi-GPU MPI runs.
     * ---------------------------------------------------------- */
    if (!cbInfo || !cbInfo->context) return;

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
     *    one-shot clock calibration.
     *    cupti_activity_init_once() is safe here: a CUDA context exists
     *    (this is a CUDA API callback), making cuptiActivityEnable valid. */
    cuda_ensure_thread_init();
    cupti_activity_init_once();
    cuda_emit_clock_calibration_once();

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
        if (!cbInfo->symbolName && !cbInfo->functionName) return;

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
            lexgion_record_t *record = lexgion_begin(
                CUDA_LEXGION, CUDA_EVENT_KERNEL_LAUNCH, codeptr);
            lexgion_t *lgp = record->lgp;

            lexgion_set_top_trace_bit_domain_event(
                lgp, CUDA_domain_index, CUDA_EVENT_KERNEL_LAUNCH);

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
                dim.gridx = grid.x; dim.gridy = grid.y; dim.gridz = grid.z;
                dim.blockx = block.x; dim.blocky = block.y; dim.blockz = block.z;
#ifdef PINSIGHT_BACKTRACE
                retrieve_backtrace();
#endif
                lttng_ust_tracepoint(cupti_pinsight_lttng_ust,
                    cudaKernelLaunch_begin, devId, correlationId, timeStamp,
                    codeptr, kernelName, &ctxStreamId, &dim);
            }
        } else if (cbInfo->callbackSite == CUPTI_API_EXIT) {
            lexgion_t *lgp = lexgion_end(NULL);
            if (lgp && PINSIGHT_SHOULD_TRACE(CUDA_domain_index) &&
                lgp->trace_bit) {
                uint64_t timeStamp = cuda_fast_timestamp();
                lttng_ust_tracepoint(cupti_pinsight_lttng_ust,
                    cudaKernelLaunch_end, devId, correlationId, timeStamp,
                    codeptr, kernelName);
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
            lexgion_record_t *record = lexgion_begin(
                CUDA_LEXGION, event_id, codeptr);
            lexgion_t *lgp = record->lgp;

            lexgion_set_top_trace_bit_domain_event(
                lgp, CUDA_domain_index, event_id);

            if (PINSIGHT_SHOULD_TRACE(CUDA_domain_index) && lgp->trace_bit) {
                const char *funName = cbInfo->functionName;
                void *dst = funcParams->dst;
                const void *src = funcParams->src;
                unsigned int count = funcParams->count;
                uint64_t timeStamp = cuda_fast_timestamp();
#ifdef PINSIGHT_BACKTRACE
                retrieve_backtrace();
#endif
                lttng_ust_tracepoint(cupti_pinsight_lttng_ust,
                    cudaMemcpy_begin, devId, correlationId, timeStamp,
                    codeptr, funName, dst, src, count, kind);
            }
        } else if (cbInfo->callbackSite == CUPTI_API_EXIT) {
            lexgion_t *lgp = lexgion_end(NULL);
            if (lgp && PINSIGHT_SHOULD_TRACE(CUDA_domain_index) &&
                lgp->trace_bit) {
                int return_val = *((int *)cbInfo->functionReturnValue);
                const char *funName = cbInfo->functionName;
                uint64_t timeStamp = cuda_fast_timestamp();
                lttng_ust_tracepoint(cupti_pinsight_lttng_ust,
                    cudaMemcpy_end, devId, correlationId, timeStamp,
                    codeptr, funName, return_val);
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

            lexgion_record_t *record = lexgion_begin(
                CUDA_LEXGION, event_id, codeptr);
            lexgion_t *lgp = record->lgp;

            lexgion_set_top_trace_bit_domain_event(
                lgp, CUDA_domain_index, event_id);

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
                lttng_ust_tracepoint(cupti_pinsight_lttng_ust,
                    cudaMemcpyAsync_begin, devId, correlationId, timeStamp,
                    codeptr, funName, dst, src, count, kind, &ctxStreamId);
            }
        } else if (cbInfo->callbackSite == CUPTI_API_EXIT) {
            lexgion_t *lgp = lexgion_end(NULL);
            if (lgp && PINSIGHT_SHOULD_TRACE(CUDA_domain_index) &&
                lgp->trace_bit) {
                int return_val = *((int *)cbInfo->functionReturnValue);
                const char *funName = cbInfo->functionName;
                uint64_t timeStamp = cuda_fast_timestamp();
                lttng_ust_tracepoint(cupti_pinsight_lttng_ust,
                    cudaMemcpyAsync_end, devId, correlationId, timeStamp,
                    codeptr, funName, return_val);
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
            lexgion_record_t *record = lexgion_begin(
                CUDA_LEXGION, CUDA_EVENT_DEVICE_SYNCHRONIZE, codeptr);
            lexgion_t *lgp = record->lgp;

            lexgion_set_top_trace_bit_domain_event(
                lgp, CUDA_domain_index, CUDA_EVENT_DEVICE_SYNCHRONIZE);

            if (PINSIGHT_SHOULD_TRACE(CUDA_domain_index) && lgp->trace_bit) {
                uint64_t timeStamp = cuda_fast_timestamp();
#ifdef PINSIGHT_BACKTRACE
                retrieve_backtrace();
#endif
                lttng_ust_tracepoint(cupti_pinsight_lttng_ust,
                    cudaDeviceSync_begin, devId, correlationId, timeStamp,
                    codeptr, funName);
            }
        } else if (cbInfo->callbackSite == CUPTI_API_EXIT) {
            lexgion_t *lgp = lexgion_end(NULL);
            if (lgp && PINSIGHT_SHOULD_TRACE(CUDA_domain_index) &&
                lgp->trace_bit) {
                int return_val = *((int *)cbInfo->functionReturnValue);
                uint64_t timeStamp = cuda_fast_timestamp();
                lttng_ust_tracepoint(cupti_pinsight_lttng_ust,
                    cudaDeviceSync_end, devId, correlationId, timeStamp,
                    codeptr, funName, return_val);
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
            ctxStreamId.streamId  = streamId;

            lexgion_record_t *record = lexgion_begin(
                CUDA_LEXGION, CUDA_EVENT_STREAM_SYNCHRONIZE, codeptr);
            lexgion_t *lgp = record->lgp;

            lexgion_set_top_trace_bit_domain_event(
                lgp, CUDA_domain_index, CUDA_EVENT_STREAM_SYNCHRONIZE);

            if (PINSIGHT_SHOULD_TRACE(CUDA_domain_index) && lgp->trace_bit) {
                uint64_t timeStamp = cuda_fast_timestamp();
#ifdef PINSIGHT_BACKTRACE
                retrieve_backtrace();
#endif
                lttng_ust_tracepoint(cupti_pinsight_lttng_ust,
                    cudaStreamSync_begin, devId, correlationId, timeStamp,
                    codeptr, funName, &ctxStreamId);
            }
        } else if (cbInfo->callbackSite == CUPTI_API_EXIT) {
            lexgion_t *lgp = lexgion_end(NULL);
            if (lgp && PINSIGHT_SHOULD_TRACE(CUDA_domain_index) &&
                lgp->trace_bit) {
                int return_val = *((int *)cbInfo->functionReturnValue);
                uint64_t timeStamp = cuda_fast_timestamp();
                lttng_ust_tracepoint(cupti_pinsight_lttng_ust,
                    cudaStreamSync_end, devId, correlationId, timeStamp,
                    codeptr, funName, return_val);
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
            cuptiActivityFlushAll(1);  /* 1 = blocking */
        }
        return;
    }

    /* Remaining stubs — deprecated or not useful for perf analysis */
    if (cbid == CUPTI_RUNTIME_TRACE_CBID_cudaThreadSynchronize_v3020 ||
        cbid == CUPTI_RUNTIME_TRACE_CBID_cudaStreamCreate_v3020 ||
        cbid == CUPTI_RUNTIME_TRACE_CBID_cudaThreadExit_v3020 ||
        cbid == CUPTI_RUNTIME_TRACE_CBID_cudaConfigureCall_v3020) {
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

#define CUPTI_ACTIVITY_BUFFER_SIZE  (8 * 1024 * 1024)   /* 8 MB per buffer */
#define CUPTI_ACTIVITY_BUFFER_ALIGN 8

static void CUPTIAPI activity_bufferRequested(uint8_t **buffer, size_t *size,
                                              size_t *maxNumRecords) {
    *maxNumRecords = 0;  /* unlimited */
    *size = CUPTI_ACTIVITY_BUFFER_SIZE;
    if (posix_memalign((void **)buffer, CUPTI_ACTIVITY_BUFFER_ALIGN,
                       CUPTI_ACTIVITY_BUFFER_SIZE) != 0)
        *buffer = NULL;
}

static void CUPTIAPI activity_bufferCompleted(CUcontext ctx, uint32_t streamId,
                                              uint8_t *buffer, size_t size,
                                              size_t validSize) {
    if (!buffer) return;
    CUpti_Activity *record = NULL;

    do {
        CUptiResult status = cuptiActivityGetNextRecord(buffer, validSize, &record);
        if (status != CUPTI_SUCCESS) break;

        switch (record->kind) {

        case CUPTI_ACTIVITY_KIND_MEMCPY: {
            /* GPU-side timing for both cudaMemcpy and cudaMemcpyAsync.
             * For synchronous cudaMemcpy, this duplicates the callback
             * tracepoint but confirms GPU-side timing — harmless.
             * For cudaMemcpyAsync, this is the only source of actual
             * transfer time. */
            CUpti_ActivityMemcpy6 *m = (CUpti_ActivityMemcpy6 *)record;
            lttng_ust_tracepoint(cupti_pinsight_lttng_ust, cudaMemcpyActivity,
                m->deviceId, m->correlationId,
                m->start, m->end, m->bytes,
                (int)m->copyKind, m->contextId, m->streamId);
            break;
        }

        case CUPTI_ACTIVITY_KIND_KERNEL:
        case CUPTI_ACTIVITY_KIND_CONCURRENT_KERNEL: {
            /* Actual GPU execution window — not just CPU-side launch time. */
            CUpti_ActivityKernel10 *k = (CUpti_ActivityKernel10 *)record;
            lttng_ust_tracepoint(cupti_pinsight_lttng_ust, cudaKernelActivity,
                k->deviceId, k->correlationId,
                k->start, k->end,
                k->contextId, k->streamId,
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
    cuptiEnableCallback(enable, subscriber, CUPTI_CB_DOMAIN_RUNTIME_API,
                        CUPTI_RUNTIME_TRACE_CBID_cudaLaunch_v3020);
    cuptiEnableCallback(enable, subscriber, CUPTI_CB_DOMAIN_RUNTIME_API,
                        CUPTI_RUNTIME_TRACE_CBID_cudaLaunchKernel_v7000);
    cuptiEnableCallback(enable, subscriber, CUPTI_CB_DOMAIN_RUNTIME_API,
                        CUPTI_RUNTIME_TRACE_CBID_cudaConfigureCall_v3020);
    cuptiEnableCallback(enable, subscriber, CUPTI_CB_DOMAIN_RUNTIME_API,
                        CUPTI_RUNTIME_TRACE_CBID_cudaThreadSynchronize_v3020);
    cuptiEnableCallback(enable, subscriber, CUPTI_CB_DOMAIN_RUNTIME_API,
                        CUPTI_RUNTIME_TRACE_CBID_cudaStreamCreate_v3020);
    cuptiEnableCallback(enable, subscriber, CUPTI_CB_DOMAIN_RUNTIME_API,
                        CUPTI_RUNTIME_TRACE_CBID_cudaStreamSynchronize_v3020);
    cuptiEnableCallback(enable, subscriber, CUPTI_CB_DOMAIN_RUNTIME_API,
                        CUPTI_RUNTIME_TRACE_CBID_cudaDeviceSynchronize_v3020);
    cuptiEnableCallback(enable, subscriber, CUPTI_CB_DOMAIN_RUNTIME_API,
                        CUPTI_RUNTIME_TRACE_CBID_cudaMemcpy_v3020);
    cuptiEnableCallback(enable, subscriber, CUPTI_CB_DOMAIN_RUNTIME_API,
                        CUPTI_RUNTIME_TRACE_CBID_cudaMemcpyAsync_v3020);
    cuptiEnableCallback(enable, subscriber, CUPTI_CB_DOMAIN_RUNTIME_API,
                        CUPTI_RUNTIME_TRACE_CBID_cudaDeviceReset_v3020);
    cuptiEnableCallback(enable, subscriber, CUPTI_CB_DOMAIN_RUNTIME_API,
                        CUPTI_RUNTIME_TRACE_CBID_cudaThreadExit_v3020);
}

/**
 * Called by the control thread to apply CUDA domain mode changes.
 * Enables callbacks when TRACING/MONITORING, disables when OFF.
 */
void pinsight_control_cuda_apply_mode(void) {
    int mode = domain_default_trace_config[CUDA_domain_index].mode;
    int enable = PINSIGHT_DOMAIN_ACTIVE(mode) ? 1 : 0;
    cupti_set_all_callbacks(enable);
}

void LTTNG_CUPTI_Init(void) {
    /* Register the CUPTI subscriber (safe before any CUDA context exists —
     * it only registers a function pointer with the CUPTI library). */
    cuptiSubscribe(&subscriber,
                   (CUpti_CallbackFunc)CUPTI_callback_lttng, NULL);

    /* Enable callbacks based on initial CUDA domain mode.
     * When starting in OFF mode, no callbacks are enabled — zero overhead.
     * Control thread will re-enable them on mode change. */
    pinsight_control_cuda_apply_mode();

    /* NOTE: Activity API (cuptiActivityEnable) requires an active CUDA
     * context and MUST NOT be called at library load time in multi-GPU
     * MPI runs where no context exists yet.  It is deferred to
     * cupti_activity_init_once(), called from the first CUPTI callback.
     * Clock calibration similarly deferred — see cuda_emit_clock_calibration_once(). */
}

void LTTNG_CUPTI_Fini(void) {
    /* Flush all pending activity records before unsubscribing.
     * Flag 1 = blocking: waits for all bufferCompleted callbacks to finish
     * before returning.  Critical for AMReX/Castro which does not call
     * cudaDeviceReset() — without this, GPU activity records may not be
     * delivered to LTTng before the session daemon stops. */
    cuptiActivityFlushAll(1);
    cuptiUnsubscribe(subscriber);
}
