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
#include "trace_domain_CUDA.h"

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
 * Starts at 1000 to avoid colliding with OpenMP thread IDs (0..N-1).
 * Each OS thread has its own TLS copy of pinsight_thread_data, so
 * the only shared state is this counter — hence the atomic. */
static _Atomic int cuda_thread_id_counter = 2000;

/**
 * Ensure pinsight_thread_data is initialized for the calling thread.
 *
 * WHY here, not in LTTNG_CUPTI_Init:
 *   LTTNG_CUPTI_Init runs on one thread. pinsight_thread_data is TLS —
 *   each OS thread has its own copy. Worker threads and CUDA driver
 *   threads don't exist at init time; the first CUPTI callback on
 *   each thread is the earliest safe initialization point.
 *
 * WHY not always 0:
 *   Multiple host threads can each own a device/context and issue
 *   CUDA calls concurrently. Lexgion bookkeeping (stack, counters)
 *   is per-thread so colliding IDs cause diagnostic confusion.
 *   OpenMP threads get IDs 0..N-1 via on_ompt_callback_thread_begin.
 *   Non-OpenMP threads get IDs 2000+ from the atomic counter here.
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

/* Fired exactly once on the first CUDA API call, guaranteeing LTTng probes
 * are registered and the session is active.  Records both CLOCK_MONOTONIC
 * and cuptiGetTimestamp() at the same instant so analysis tools can compute
 * the constant clock offset and align GPU activity records with CPU events. */
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
     * 1. Deferred reconfig handler — mirrors OpenMP parallel_begin.
     *    Fires on the next CUDA API call after SIGUSR1 or auto-trigger.
     *    Only run at ENTER to avoid duplicate reloads per call.
     *
     *    Optimistic fast-path: plain volatile read first; atomic exchange
     *    only when the flag appears set.  A rare missed read just defers
     *    reconfig by one CUDA call — acceptable.
     * ---------------------------------------------------------- */
    if (cbInfo->callbackSite == CUPTI_API_ENTER) {
        int need_reregister = 0;

        if (config_reload_requested &&
            __atomic_exchange_n(&config_reload_requested, 0,
                                __ATOMIC_SEQ_CST)) {
            pinsight_load_trace_config(NULL);
            domain_default_trace_config[CUDA_domain_index].mode_change_fired = 0;
            need_reregister = 1;
        }

        if (mode_change_requested &&
            __atomic_exchange_n(&mode_change_requested, 0,
                                __ATOMIC_SEQ_CST)) {
            need_reregister = 1;
        }

        if (need_reregister) {
            /* For CUDA, callbacks are always active — nothing to re-register.
             * Config was already reloaded above; killswitch picks up new mode. */
        }
    }

    /* ----------------------------------------------------------
     * 2. Domain mode check — OFF → skip everything
     * ---------------------------------------------------------- */
    if (!PINSIGHT_DOMAIN_ACTIVE(
            domain_default_trace_config[CUDA_domain_index].mode))
        return;

    /* 3. Only runtime API domain */
    if (domain != CUPTI_CB_DOMAIN_RUNTIME_API)
        return;

    /* 4. Ensure thread data initialized + one-shot clock calibration */
    cuda_ensure_thread_init();
    cuda_emit_clock_calibration_once();

    /* ----------------------------------------------------------
     * 5. Extract common callback info
     * ---------------------------------------------------------- */
    const CUcontext context = cbInfo->context;
    unsigned int cxtId;
    cuptiGetContextId(context, &cxtId);
    unsigned int devId;
    cuptiGetDeviceId(context, &devId);
    unsigned int correlationId = cbInfo->correlationId;

    /* ----------------------------------------------------------
     * 6. Per-cbid dispatch
     * ---------------------------------------------------------- */

    /* ========== Kernel Launch ========== */
    if (cbid == CUPTI_RUNTIME_TRACE_CBID_cudaLaunch_v3020 ||
        cbid == CUPTI_RUNTIME_TRACE_CBID_cudaLaunchKernel_v7000) {
        /* __builtin_return_address(N): N is the call depth from user code
         * to this CUPTI callback through the CUDA runtime/driver stack.
         *
         * WARNING: This depth is driver-version-dependent and fragile.
         * NVIDIA does not officially support stack walking inside CUPTI
         * callbacks — internal inlining and dispatch changes across
         * driver versions can shift frame counts.  Must re-verify with
         * addr2line after any CUDA toolkit/driver upgrade.
         *
         * Verified on A100 + driver 580.126 + CUDA 13.0:
         *   cudaLaunchKernel call chain adds 3 stub frames:
         *     depth 8: __cudaLaunchKernel_helper (device_functions.h)
         *     depth 9: __device_stub__KernelName  (cudafe1.stub.c)
         *     depth 10: KernelName host stub      (user .cu file)
         *     depth 11: actual user call site      ← THIS ONE
         *   If depth is wrong, all lexgions collapse into one.
         *   The correlationId from CUPTI can serve as a fallback key.
         */
        const void *codeptr = __builtin_return_address(11);
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
                uint64_t timeStamp;
                cuptiGetTimestamp(&timeStamp);
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
                uint64_t timeStamp;
                cuptiGetTimestamp(&timeStamp);
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
        /* Verified on A100 + driver 580.126 + CUDA 13.0: depth 8 = user code */
        const void *codeptr = __builtin_return_address(8);
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
                uint64_t timeStamp;
                cuptiGetTimestamp(&timeStamp);
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
                uint64_t timeStamp;
                cuptiGetTimestamp(&timeStamp);
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
        /* Verified on A100 + driver 580.126 + CUDA 13.0: depth 8 = user code */
        const void *codeptr = __builtin_return_address(8);
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
                uint64_t timeStamp;
                cuptiGetTimestamp(&timeStamp);
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
                uint64_t timeStamp;
                cuptiGetTimestamp(&timeStamp);
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
        /* Verified on A100 + driver 580.126 + CUDA 13.0: depth 8 = user code */
        const void *codeptr = __builtin_return_address(8);
        const char *funName = cbInfo->functionName;

        if (cbInfo->callbackSite == CUPTI_API_ENTER) {
            lexgion_record_t *record = lexgion_begin(
                CUDA_LEXGION, CUDA_EVENT_DEVICE_SYNCHRONIZE, codeptr);
            lexgion_t *lgp = record->lgp;

            lexgion_set_top_trace_bit_domain_event(
                lgp, CUDA_domain_index, CUDA_EVENT_DEVICE_SYNCHRONIZE);

            if (PINSIGHT_SHOULD_TRACE(CUDA_domain_index) && lgp->trace_bit) {
                uint64_t timeStamp;
                cuptiGetTimestamp(&timeStamp);
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
                uint64_t timeStamp;
                cuptiGetTimestamp(&timeStamp);
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
        /* Verified on A100 + driver 580.126 + CUDA 13.0: depth 8 = user code */
        const void *codeptr = __builtin_return_address(8);
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
                uint64_t timeStamp;
                cuptiGetTimestamp(&timeStamp);
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
                uint64_t timeStamp;
                cuptiGetTimestamp(&timeStamp);
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

void LTTNG_CUPTI_Init(void) {
    /* Create subscriber — lives for the entire program lifetime */
    cuptiSubscribe(&subscriber,
                   (CUpti_CallbackFunc)CUPTI_callback_lttng, NULL);

    /* Enable callbacks for the CUDA runtime API events we handle */
    cuptiEnableCallback(1, subscriber, CUPTI_CB_DOMAIN_RUNTIME_API,
                        CUPTI_RUNTIME_TRACE_CBID_cudaLaunch_v3020);
    cuptiEnableCallback(1, subscriber, CUPTI_CB_DOMAIN_RUNTIME_API,
                        CUPTI_RUNTIME_TRACE_CBID_cudaLaunchKernel_v7000);
    cuptiEnableCallback(1, subscriber, CUPTI_CB_DOMAIN_RUNTIME_API,
                        CUPTI_RUNTIME_TRACE_CBID_cudaConfigureCall_v3020);
    cuptiEnableCallback(1, subscriber, CUPTI_CB_DOMAIN_RUNTIME_API,
                        CUPTI_RUNTIME_TRACE_CBID_cudaThreadSynchronize_v3020);
    cuptiEnableCallback(1, subscriber, CUPTI_CB_DOMAIN_RUNTIME_API,
                        CUPTI_RUNTIME_TRACE_CBID_cudaStreamCreate_v3020);
    cuptiEnableCallback(1, subscriber, CUPTI_CB_DOMAIN_RUNTIME_API,
                        CUPTI_RUNTIME_TRACE_CBID_cudaStreamSynchronize_v3020);
    cuptiEnableCallback(1, subscriber, CUPTI_CB_DOMAIN_RUNTIME_API,
                        CUPTI_RUNTIME_TRACE_CBID_cudaDeviceSynchronize_v3020);
    cuptiEnableCallback(1, subscriber, CUPTI_CB_DOMAIN_RUNTIME_API,
                        CUPTI_RUNTIME_TRACE_CBID_cudaMemcpy_v3020);
    cuptiEnableCallback(1, subscriber, CUPTI_CB_DOMAIN_RUNTIME_API,
                        CUPTI_RUNTIME_TRACE_CBID_cudaMemcpyAsync_v3020);
    cuptiEnableCallback(1, subscriber, CUPTI_CB_DOMAIN_RUNTIME_API,
                        CUPTI_RUNTIME_TRACE_CBID_cudaDeviceReset_v3020);
    cuptiEnableCallback(1, subscriber, CUPTI_CB_DOMAIN_RUNTIME_API,
                        CUPTI_RUNTIME_TRACE_CBID_cudaThreadExit_v3020);

    /* All selected callbacks are always active for the program lifetime.
     * Mode control uses the killswitch at the top of CUPTI_callback_lttng.
     * No need to call pinsight_register_cuda_callbacks — it is removed. */

    /* --- Activity API: GPU-side timestamps for async operations --- */
    cuptiActivityRegisterCallbacks(activity_bufferRequested,
                                   activity_bufferCompleted);
    cuptiActivityEnable(CUPTI_ACTIVITY_KIND_MEMCPY);
    cuptiActivityEnable(CUPTI_ACTIVITY_KIND_KERNEL);
    cuptiActivityEnable(CUPTI_ACTIVITY_KIND_CONCURRENT_KERNEL);

    /* Clock calibration fires on the first CUDA API callback — see
     * cuda_emit_clock_calibration_once() near the top of this file. */
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
