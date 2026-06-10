#include "pinsight.h"
#include "pinsight_control_thread.h"
#include "trace_config.h"
#include <hip/hip_runtime.h>
#include <roctracer/roctracer.h>
#include <roctracer/roctracer_hip.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

int HIP_domain_index;
domain_info_t  *HIP_domain_info;
domain_trace_config_t *HIP_trace_config;

#define LTTNG_UST_TRACEPOINT_CREATE_PROBES
#define LTTNG_UST_TRACEPOINT_DEFINE
#include "roctracer_lttng_ust_tracepoint.h"

/* ================================================================
 * Event ID constants — must match the dense IDs assigned in
 * trace_domain_HIP.h's DSL definition (declaration order).
 * ================================================================ */
#define HIP_EVENT_DEVICE_RESET         0
#define HIP_EVENT_DEVICE_SYNCHRONIZE   1
#define HIP_EVENT_STREAM_SYNCHRONIZE  12
#define HIP_EVENT_MEMCPY_HTOD         25
#define HIP_EVENT_MEMCPY_DTOH         26
#define HIP_EVENT_MEMCPY_DTOD         27
#define HIP_EVENT_MEMCPY_HTOH         28
#define HIP_EVENT_MEMCPY_ASYNC        29
#define HIP_EVENT_KERNEL_LAUNCH       32

/* ================================================================
 * Helper functions
 * ================================================================ */

int HIP_get_device_id(void *arg) {
    int dev;
    hipGetDevice(&dev);
    return dev;
}

static inline int hip_memcpy_event_id(int kind) {
    switch (kind) {
    case hipMemcpyHostToDevice:   return HIP_EVENT_MEMCPY_HTOD;
    case hipMemcpyDeviceToHost:   return HIP_EVENT_MEMCPY_DTOH;
    case hipMemcpyDeviceToDevice: return HIP_EVENT_MEMCPY_DTOD;
    default:                      return HIP_EVENT_MEMCPY_HTOH;
    }
}

/* Thread IDs for pure HIP host threads start at 4000 to avoid collisions:
 *   OpenMP: 0+, CUDA: 2000+, Python: 3000+, HIP: 4000+ */
static _Atomic int hip_thread_id_counter = 4000;

static inline void hip_ensure_thread_init(void) {
    if (!pinsight_thread_data.initialized) {
        int tid = __atomic_fetch_add(&hip_thread_id_counter, 1, __ATOMIC_RELAXED);
        init_thread_data(tid);
    }
}

/* ================================================================
 * Per-thread device ID cache
 *
 * hipGetDevice() is cheap (~50 ns) but called on every callback.
 * Cache the result per-thread; invalidate when it changes (rare).
 * ================================================================ */
static __thread int hip_tls_dev_cached = 0;
static __thread int hip_tls_devId = 0;

static inline int hip_get_cached_device(void) {
    if (__builtin_expect(hip_tls_dev_cached, 1))
        return hip_tls_devId;
    hipGetDevice(&hip_tls_devId);
    hip_tls_dev_cached = 1;
    return hip_tls_devId;
}

/* ================================================================
 * Fast timestamp
 *
 * roctracer_get_timestamp() cost is similar to cuptiGetTimestamp().
 * On MI300A, CPU and GPU share the same clock domain, so the offset
 * between roctracer_get_timestamp() and CLOCK_MONOTONIC is typically
 * near zero — but we still calibrate once for analysis tool alignment.
 * ================================================================ */
static int64_t  roctracer_clock_offset_ns = 0;
static int      calib_done = 0; /* plain int for __atomic builtins */

static inline uint64_t hip_fast_timestamp(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    uint64_t mono_ns = (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
    return (uint64_t)((int64_t)mono_ns + roctracer_clock_offset_ns);
}

/* Compute the CLOCK_MONOTONIC -> roctracer/HSA clock offset and emit the
 * calibration anchor, exactly once, on the first callback.
 *
 * This MUST be deferred to a callback, not done at init: roctracer_get_timestamp()
 * returns 0 until the HSA runtime is initialized (which happens on the first HIP
 * call), and the LTTng provider is likewise not registered at library-constructor
 * time.  Both are ready by the first callback.  Records both clocks at the same
 * instant so analysis tools can align GPU activity records with CPU events. */
static inline void hip_calibrate_once(void) {
    /* Fast path: plain read, predicted taken once calibrated — no atomic cost.
     * A racing thread that still sees 0 is caught by the atomic exchange below,
     * so at most one thread ever does the calibration. */
    if (__builtin_expect(calib_done, 1))
        return;
    if (__atomic_exchange_n(&calib_done, 1, __ATOMIC_SEQ_CST) == 0) {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        uint64_t mono_ns = (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
        roctracer_timestamp_t roc_ts = 0;
        roctracer_get_timestamp(&roc_ts);
        roctracer_clock_offset_ns = (int64_t)roc_ts - (int64_t)mono_ns;
        lttng_ust_tracepoint(roctracer_pinsight_lttng_ust, hip_clock_calibration,
                             mono_ns, (uint64_t)roc_ts);
    }
}

/* ================================================================
 * Activity pool — captures GPU-side timestamps for async operations.
 *
 * ROCTracer uses a pool model (unlike CUPTI's caller-provided buffers):
 *   - roctracer_open_pool_expl() allocates the pool internally.
 *   - buffer_callback_fun is called when the pool is flushed.
 *   - Use roctracer_next_record() to advance — records are variable-length.
 *
 * Linked to callback records via correlation_id.
 * ================================================================ */
static roctracer_pool_t *activity_pool = NULL;

static void hip_activity_callback(const char *begin, const char *end,
                                  void *arg) {
    const roctracer_record_t *rec = (const roctracer_record_t *)begin;
    while ((const char *)rec < end) {
        if (rec->domain == ACTIVITY_DOMAIN_HIP_OPS) {
            switch (rec->op) {
            case HIP_OP_ID_DISPATCH:
                lttng_ust_tracepoint(roctracer_pinsight_lttng_ust, hipKernelActivity,
                                     rec->device_id,
                                     rec->correlation_id,
                                     rec->begin_ns,
                                     rec->end_ns,
                                     rec->queue_id,
                                     rec->kernel_name ? rec->kernel_name : "");
                break;
            case HIP_OP_ID_COPY:
                lttng_ust_tracepoint(roctracer_pinsight_lttng_ust, hipMemcpyActivity,
                                     rec->device_id,
                                     rec->correlation_id,
                                     rec->begin_ns,
                                     rec->end_ns,
                                     rec->bytes,
                                     0, /* copyKind not in activity_record_t; available only in callback API */
                                     rec->queue_id);
                break;
            default:
                break;
            }
        }
        roctracer_next_record(rec, &rec);
    }
}


/* ================================================================
 * Main ROCTracer callback
 *
 * Structure:
 *   1. Domain mode check (OFF → return)
 *   2. Only handle ACTIVITY_DOMAIN_HIP_API
 *   3. Ensure thread initialization + one-shot clock calibration
 *      + deferred activity pool init
 *   4. Cast callback_data to hip_api_data_t
 *   5. Per-cid dispatch: lexgion begin/end + rate + trace
 * ================================================================ */
static void hip_api_callback(uint32_t domain, uint32_t cid,
                             const void *callback_data, void *arg) {
    /* 1. Pause check */
    pinsight_check_pause();

    /* 2. Domain mode check */
    if (!PINSIGHT_DOMAIN_ACTIVE(
            domain_default_trace_config[HIP_domain_index].mode))
        return;

    /* 3. Fast reject: the callback fires for every HIP API call, but only a
     * handful do real work.  Skip thread init, clock calibration, and the
     * device query for everything else. */
    if (cid != HIP_API_ID_hipLaunchKernel      &&
        cid != HIP_API_ID_hipMemcpy            &&
        cid != HIP_API_ID_hipMemcpyAsync       &&
        cid != HIP_API_ID_hipDeviceSynchronize &&
        cid != HIP_API_ID_hipStreamSynchronize &&
        cid != HIP_API_ID_hipDeviceReset       &&
        cid != HIP_API_ID_hipSetDevice)
        return;

    const hip_api_data_t *api_data = (const hip_api_data_t *)callback_data;

    /* ========== hipSetDevice — invalidate the per-thread device cache so the
     * next traced event re-queries the (now changed) device.  Handled before
     * thread init / clock calibration / device query since none are needed. */
    if (cid == HIP_API_ID_hipSetDevice) {
        if (api_data->phase == ACTIVITY_API_PHASE_EXIT)
            hip_tls_dev_cached = 0;
        return;
    }

    /* 4. Thread init + one-shot clock calibration (first callback only) */
    hip_ensure_thread_init();
    hip_calibrate_once();

    int devId = hip_get_cached_device();

    /* ========== hipLaunchKernel ========== */
    if (cid == HIP_API_ID_hipLaunchKernel) {
        /* Use the host-side kernel function pointer as lexgion codeptr.
         * Unique per kernel definition, stable for the process lifetime. */
        const void *codeptr = (const void *)api_data->args.hipLaunchKernel.function_address;
        const char *kernelName = hipKernelNameRefByPtr(
            api_data->args.hipLaunchKernel.function_address,
            api_data->args.hipLaunchKernel.stream);

        if (api_data->phase == ACTIVITY_API_PHASE_ENTER) {
            lexgion_record_t *record =
                lexgion_begin(ROCL_LEXGION, HIP_EVENT_KERNEL_LAUNCH, codeptr);
            lexgion_t *lgp = record->lgp;

            if (lgp->name_resolved_gen != trace_config_change_counter) {
                lgp->name             = kernelName ? kernelName : "";
                lgp->filename_hint    = NULL;
                lgp->name_resolved_gen = trace_config_change_counter;
                lgp->trace_config_change_counter = (unsigned int)-1;
            }

            if (PINSIGHT_SHOULD_TRACE(HIP_domain_index)) {
                lexgion_set_top_trace_bit_domain_event(
                    lgp, HIP_domain_index, HIP_EVENT_KERNEL_LAUNCH);
            }

            if (PINSIGHT_SHOULD_TRACE(HIP_domain_index) && lgp->trace_bit) {
                dim3 grid  = api_data->args.hipLaunchKernel.numBlocks;
                dim3 block = api_data->args.hipLaunchKernel.dimBlocks;
                struct hip_dimension_t dim = {
                    grid.x, grid.y, grid.z,
                    block.x, block.y, block.z
                };
                uint64_t ts = hip_fast_timestamp();
#ifdef PINSIGHT_BACKTRACE
                retrieve_backtrace();
#endif
                lttng_ust_tracepoint(roctracer_pinsight_lttng_ust,
                                     hipKernelLaunch_begin,
                                     devId, api_data->correlation_id, ts,
                                     codeptr, kernelName ? kernelName : "",
                                     &dim);
            }
        } else if (api_data->phase == ACTIVITY_API_PHASE_EXIT) {
            lexgion_t *lgp = lexgion_end(NULL);
            if (lgp && PINSIGHT_SHOULD_TRACE(HIP_domain_index) && lgp->trace_bit) {
                uint64_t ts = hip_fast_timestamp();
                lttng_ust_tracepoint(roctracer_pinsight_lttng_ust,
                                     hipKernelLaunch_end,
                                     devId, api_data->correlation_id, ts,
                                     codeptr, kernelName ? kernelName : "");
                lexgion_post_trace_update(lgp);
            }
        }
        return;
    }

    /* ========== hipMemcpy (synchronous) ========== */
    if (cid == HIP_API_ID_hipMemcpy) {
        const void *codeptr = (const void *)api_data->args.hipMemcpy.dst;
        /* Use a stable per-kind codeptr: point to the event name string */
        int kind     = (int)api_data->args.hipMemcpy.kind;
        int event_id = hip_memcpy_event_id(kind);
        codeptr = (const void *)domain_info_table[HIP_domain_index]
                      .event_table[event_id].name;

        if (api_data->phase == ACTIVITY_API_PHASE_ENTER) {
            lexgion_record_t *record =
                lexgion_begin(ROCL_LEXGION, event_id, codeptr);
            lexgion_t *lgp = record->lgp;

            if (lgp->name_resolved_gen != trace_config_change_counter) {
                lgp->name = domain_info_table[HIP_domain_index]
                                .event_table[event_id].name;
                lgp->filename_hint    = NULL;
                lgp->name_resolved_gen = trace_config_change_counter;
                lgp->trace_config_change_counter = (unsigned int)-1;
            }

            if (PINSIGHT_SHOULD_TRACE(HIP_domain_index))
                lexgion_set_top_trace_bit_domain_event(
                    lgp, HIP_domain_index, event_id);

            if (PINSIGHT_SHOULD_TRACE(HIP_domain_index) && lgp->trace_bit) {
                void *dst       = api_data->args.hipMemcpy.dst;
                const void *src = api_data->args.hipMemcpy.src;
                size_t count    = api_data->args.hipMemcpy.sizeBytes;
                uint64_t ts     = hip_fast_timestamp();
#ifdef PINSIGHT_BACKTRACE
                retrieve_backtrace();
#endif
                lttng_ust_tracepoint(roctracer_pinsight_lttng_ust,
                                     hipMemcpy_begin,
                                     devId, api_data->correlation_id, ts,
                                     codeptr, lgp->name,
                                     dst, src, count, kind);
            }
        } else if (api_data->phase == ACTIVITY_API_PHASE_EXIT) {
            lexgion_t *lgp = lexgion_end(NULL);
            if (lgp && PINSIGHT_SHOULD_TRACE(HIP_domain_index) && lgp->trace_bit) {
                int rv      = 0; /* hip_api_data_t has no retval field */
                uint64_t ts = hip_fast_timestamp();
                lttng_ust_tracepoint(roctracer_pinsight_lttng_ust,
                                     hipMemcpy_end,
                                     devId, api_data->correlation_id, ts,
                                     codeptr, lgp->name, rv);
                lexgion_post_trace_update(lgp);
            }
        }
        return;
    }

    /* ========== hipMemcpyAsync ========== */
    if (cid == HIP_API_ID_hipMemcpyAsync) {
        int kind     = (int)api_data->args.hipMemcpyAsync.kind;
        int event_id = HIP_EVENT_MEMCPY_ASYNC;
        const void *codeptr = (const void *)domain_info_table[HIP_domain_index]
                                  .event_table[event_id].name;

        if (api_data->phase == ACTIVITY_API_PHASE_ENTER) {
            lexgion_record_t *record =
                lexgion_begin(ROCL_LEXGION, event_id, codeptr);
            lexgion_t *lgp = record->lgp;

            if (lgp->name_resolved_gen != trace_config_change_counter) {
                lgp->name = domain_info_table[HIP_domain_index]
                                .event_table[event_id].name;
                lgp->filename_hint    = NULL;
                lgp->name_resolved_gen = trace_config_change_counter;
                lgp->trace_config_change_counter = (unsigned int)-1;
            }

            if (PINSIGHT_SHOULD_TRACE(HIP_domain_index))
                lexgion_set_top_trace_bit_domain_event(
                    lgp, HIP_domain_index, event_id);

            if (PINSIGHT_SHOULD_TRACE(HIP_domain_index) && lgp->trace_bit) {
                void *dst       = api_data->args.hipMemcpyAsync.dst;
                const void *src = api_data->args.hipMemcpyAsync.src;
                size_t count    = api_data->args.hipMemcpyAsync.sizeBytes;
                uint64_t ts     = hip_fast_timestamp();
#ifdef PINSIGHT_BACKTRACE
                retrieve_backtrace();
#endif
                lttng_ust_tracepoint(roctracer_pinsight_lttng_ust,
                                     hipMemcpyAsync_begin,
                                     devId, api_data->correlation_id, ts,
                                     codeptr, lgp->name,
                                     dst, src, count, kind);
            }
        } else if (api_data->phase == ACTIVITY_API_PHASE_EXIT) {
            lexgion_t *lgp = lexgion_end(NULL);
            if (lgp && PINSIGHT_SHOULD_TRACE(HIP_domain_index) && lgp->trace_bit) {
                int rv      = 0; /* hip_api_data_t has no retval field */
                uint64_t ts = hip_fast_timestamp();
                lttng_ust_tracepoint(roctracer_pinsight_lttng_ust,
                                     hipMemcpyAsync_end,
                                     devId, api_data->correlation_id, ts,
                                     codeptr, lgp->name, rv);
                lexgion_post_trace_update(lgp);
            }
        }
        return;
    }

    /* ========== hipDeviceSynchronize ========== */
    if (cid == HIP_API_ID_hipDeviceSynchronize) {
        const void *codeptr = (const void *)domain_info_table[HIP_domain_index]
                                  .event_table[HIP_EVENT_DEVICE_SYNCHRONIZE].name;

        if (api_data->phase == ACTIVITY_API_PHASE_ENTER) {
            lexgion_record_t *record =
                lexgion_begin(ROCL_LEXGION, HIP_EVENT_DEVICE_SYNCHRONIZE, codeptr);
            lexgion_t *lgp = record->lgp;

            if (lgp->name_resolved_gen != trace_config_change_counter) {
                lgp->name = domain_info_table[HIP_domain_index]
                                .event_table[HIP_EVENT_DEVICE_SYNCHRONIZE].name;
                lgp->filename_hint    = NULL;
                lgp->name_resolved_gen = trace_config_change_counter;
                lgp->trace_config_change_counter = (unsigned int)-1;
            }

            if (PINSIGHT_SHOULD_TRACE(HIP_domain_index))
                lexgion_set_top_trace_bit_domain_event(
                    lgp, HIP_domain_index, HIP_EVENT_DEVICE_SYNCHRONIZE);

            if (PINSIGHT_SHOULD_TRACE(HIP_domain_index) && lgp->trace_bit) {
                uint64_t ts = hip_fast_timestamp();
#ifdef PINSIGHT_BACKTRACE
                retrieve_backtrace();
#endif
                lttng_ust_tracepoint(roctracer_pinsight_lttng_ust,
                                     hipDeviceSync_begin,
                                     devId, api_data->correlation_id, ts,
                                     codeptr, lgp->name);
            }
        } else if (api_data->phase == ACTIVITY_API_PHASE_EXIT) {
            lexgion_t *lgp = lexgion_end(NULL);
            if (lgp && PINSIGHT_SHOULD_TRACE(HIP_domain_index) && lgp->trace_bit) {
                int rv      = 0; /* hip_api_data_t has no retval field */
                uint64_t ts = hip_fast_timestamp();
                lttng_ust_tracepoint(roctracer_pinsight_lttng_ust,
                                     hipDeviceSync_end,
                                     devId, api_data->correlation_id, ts,
                                     codeptr, lgp->name, rv);
                lexgion_post_trace_update(lgp);
            }
        }
        return;
    }

    /* ========== hipStreamSynchronize ========== */
    if (cid == HIP_API_ID_hipStreamSynchronize) {
        const void *codeptr = (const void *)domain_info_table[HIP_domain_index]
                                  .event_table[HIP_EVENT_STREAM_SYNCHRONIZE].name;

        if (api_data->phase == ACTIVITY_API_PHASE_ENTER) {
            lexgion_record_t *record =
                lexgion_begin(ROCL_LEXGION, HIP_EVENT_STREAM_SYNCHRONIZE, codeptr);
            lexgion_t *lgp = record->lgp;

            if (lgp->name_resolved_gen != trace_config_change_counter) {
                lgp->name = domain_info_table[HIP_domain_index]
                                .event_table[HIP_EVENT_STREAM_SYNCHRONIZE].name;
                lgp->filename_hint    = NULL;
                lgp->name_resolved_gen = trace_config_change_counter;
                lgp->trace_config_change_counter = (unsigned int)-1;
            }

            if (PINSIGHT_SHOULD_TRACE(HIP_domain_index))
                lexgion_set_top_trace_bit_domain_event(
                    lgp, HIP_domain_index, HIP_EVENT_STREAM_SYNCHRONIZE);

            if (PINSIGHT_SHOULD_TRACE(HIP_domain_index) && lgp->trace_bit) {
                /* hipStream_t is a pointer; use its numeric value as a queue ID */
                unsigned int streamId =
                    (unsigned int)(uintptr_t)
                    api_data->args.hipStreamSynchronize.stream;
                uint64_t ts = hip_fast_timestamp();
#ifdef PINSIGHT_BACKTRACE
                retrieve_backtrace();
#endif
                lttng_ust_tracepoint(roctracer_pinsight_lttng_ust,
                                     hipStreamSync_begin,
                                     devId, api_data->correlation_id, ts,
                                     codeptr, lgp->name, streamId);
            }
        } else if (api_data->phase == ACTIVITY_API_PHASE_EXIT) {
            lexgion_t *lgp = lexgion_end(NULL);
            if (lgp && PINSIGHT_SHOULD_TRACE(HIP_domain_index) && lgp->trace_bit) {
                int rv      = 0; /* hip_api_data_t has no retval field */
                uint64_t ts = hip_fast_timestamp();
                lttng_ust_tracepoint(roctracer_pinsight_lttng_ust,
                                     hipStreamSync_end,
                                     devId, api_data->correlation_id, ts,
                                     codeptr, lgp->name, rv);
                lexgion_post_trace_update(lgp);
            }
        }
        return;
    }

    /* ========== hipDeviceReset — flush pending activity records before the
     * context is destroyed, then keep the pool open for the new implicit
     * context that the next HIP call will create. ========== */
    if (cid == HIP_API_ID_hipDeviceReset) {
        if (api_data->phase == ACTIVITY_API_PHASE_ENTER && activity_pool)
            roctracer_flush_activity_expl(activity_pool);
        return;
    }
}

/* ================================================================
 * Mode management
 * ================================================================ */

static int hip_permanently_off = 0;

/* Called by the control thread for runtime mode transitions.
 * Precondition: pinsight_hip_init() has run, so last_mode is never NONE.
 * Callback and activity pool are always enabled/disabled as a pair. */
void pinsight_control_hip_apply_mode(void) {
    if (hip_permanently_off)
        return;

    pinsight_domain_mode_t mode =
        domain_default_trace_config[HIP_domain_index].mode;
    pinsight_domain_mode_t last =
        domain_default_trace_config[HIP_domain_index].last_mode;

    if (mode == last)
        return;

    if (mode == PINSIGHT_DOMAIN_OFF || mode == PINSIGHT_DOMAIN_STANDBY) {
        if (PINSIGHT_DOMAIN_ACTIVE(last)) {
            /* ACTIVE → STANDBY/OFF: disable both together */
            if (activity_pool) {
                roctracer_flush_activity_expl(activity_pool);
                roctracer_disable_domain_activity(ACTIVITY_DOMAIN_HIP_OPS);
                roctracer_close_pool_expl(activity_pool);
                activity_pool = NULL;
            }
            roctracer_disable_domain_callback(ACTIVITY_DOMAIN_HIP_API);
        }
        /* STANDBY→OFF or STANDBY→STANDBY: both already torn down, nothing to do */
        if (mode == PINSIGHT_DOMAIN_OFF) {
            hip_permanently_off = 1;
            fprintf(stderr, "PInsight: HIP domain permanently OFF\n");
        }
    } else if (!PINSIGHT_DOMAIN_ACTIVE(last)) {
        /* STANDBY → MONITORING/TRACING: enable both together */
        roctracer_enable_domain_callback(ACTIVITY_DOMAIN_HIP_API,
                                         hip_api_callback, NULL);
        roctracer_properties_t props;
        memset(&props, 0, sizeof(props));
        props.buffer_size         = 0x200000; /* 2 MB */
        props.buffer_callback_fun = hip_activity_callback;
        roctracer_open_pool_expl(&props, &activity_pool);
        roctracer_enable_domain_activity_expl(ACTIVITY_DOMAIN_HIP_OPS, activity_pool);
    }
    /* MONITORING↔TRACING: callback and pool already active; mode flag distinguishes them */
}

/* ================================================================
 * Initialization and finalization
 * ================================================================ */

void LTTNG_ROCTRACER_Init(void) {
    pinsight_domain_mode_t mode =
        domain_default_trace_config[HIP_domain_index].mode;

    if (mode == PINSIGHT_DOMAIN_OFF) {
        hip_permanently_off = 1;
        fprintf(stderr, "PInsight: HIP domain starting OFF\n");
    } else {
        /* Clock calibration is deferred to the first callback (roctracer
         * timestamp + LTTng provider are not ready at constructor time). */
        if (PINSIGHT_DOMAIN_ACTIVE(mode)) {
            roctracer_enable_domain_callback(ACTIVITY_DOMAIN_HIP_API,
                                             hip_api_callback, NULL);
            roctracer_properties_t props;
            memset(&props, 0, sizeof(props));
            props.buffer_size         = 0x200000; /* 2 MB */
            props.buffer_callback_fun = hip_activity_callback;
            roctracer_open_pool_expl(&props, &activity_pool);
            /* _expl routes records into our pool; the no-arg variant sends to
             * the default pool which has no callback and drops records. */
            roctracer_enable_domain_activity_expl(ACTIVITY_DOMAIN_HIP_OPS, activity_pool);
        }
        /* STANDBY: callback and pool stay unregistered until first active transition */
    }
    domain_default_trace_config[HIP_domain_index].last_mode = mode;
}

void LTTNG_ROCTRACER_Fini(void) {
    if (hip_permanently_off)
        return;
    if (activity_pool) {
        roctracer_flush_activity_expl(activity_pool);
        roctracer_disable_domain_activity(ACTIVITY_DOMAIN_HIP_OPS);
        roctracer_close_pool_expl(activity_pool);
        activity_pool = NULL;
    }
    roctracer_disable_domain_callback(ACTIVITY_DOMAIN_HIP_API);
    hip_permanently_off = 1;
}
