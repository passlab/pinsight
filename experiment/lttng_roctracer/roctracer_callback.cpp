/*
 * roctracer_callback.c — ROCTracer interception for HIP
 *
 * Three build phases (selected by Makefile compile flags):
 *
 *  Phase 1 — no -DWITH_ROCTRACER: stubs only, zero overhead
 *  Phase 2 — -DWITH_ROCTRACER:    real ROCTracer callbacks, printf output
 *  Phase 3 — -DWITH_ROCTRACER -DWITH_LTTNG: same + LTTng UST tracepoints
 *
 * ROCTracer has two interception layers:
 *
 *  1. Callback API  (ACTIVITY_DOMAIN_HIP_API)
 *     Synchronous. Called on the host thread that invoked the HIP function,
 *     at ENTER (before the call) and EXIT (after the call returns).
 *     Gives access to all function arguments via hip_api_data_t.
 *     Used for: kernel launch params, memcpy args, host-side timestamps.
 *
 *  2. Activity API  (ACTIVITY_DOMAIN_HIP_OPS)
 *     Asynchronous. GPU-side records written to a pool by the runtime.
 *     Flushed via a pool callback. Records carry GPU begin/end timestamps.
 *     Linked to callback records by correlation_id.
 *     Used for: actual GPU kernel execution time, actual memcpy duration.
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "roctracer_callback.h"

/* ================================================================
 * Phase 1: stubs — compiled when -DWITH_ROCTRACER is NOT set
 * ================================================================ */
#ifndef WITH_ROCTRACER

void ROCTRACER_Init(void) {}
void ROCTRACER_Fini(void) {}

#else  /* WITH_ROCTRACER */

/* ================================================================
 * Phase 2 + 3: real ROCTracer implementation
 * ================================================================ */
#include <roctracer/roctracer.h>
#include <roctracer/roctracer_hip.h>        /* hip_api_data_t, HIP_API_ID_* */
#include <roctracer/ext/prof_protocol.h>    /* activity_record_t            */
#include <hip/hip_runtime.h>                /* hipKernelNameRefByPtr, etc.  */

/* ---- Phase 3: pull in LTTng tracepoint definitions (host only) ----
 * hipcc compiles translation units for both host and GPU device (gfx942).
 * LTTng UST is purely host-side; guard it from device compilation. */
#if defined(WITH_LTTNG) && !defined(__HIP_DEVICE_COMPILE__)
#ifndef __HIP_DEVICE_COMPILE__
#define LTTNG_UST_TRACEPOINT_CREATE_PROBES
#define LTTNG_UST_TRACEPOINT_DEFINE
#include "lttng_roctracer_tracepoint.h"
#endif
#endif

/* ----------------------------------------------------------------
 * Helper: hipMemcpyKind → readable string
 * ---------------------------------------------------------------- */
static const char *memcpy_kind_str(int kind) {
    switch (kind) {
    case 0: return "HtoH";
    case 1: return "HtoD";
    case 2: return "DtoH";
    case 3: return "DtoD";
    default: return "Default";
    }
}

/* ================================================================
 * Activity pool — captures GPU-side timestamps
 *
 * ROCTracer uses a push model: the runtime fills a pool buffer and
 * calls this function when it is full or flushed.
 * Iterate with roctracer_next_record() — records are variable-length.
 *
 * ACTIVITY_DOMAIN_HIP_OPS operations:
 *   HIP_OP_ID_DISPATCH (0) — kernel execution
 *   HIP_OP_ID_COPY     (1) — memcpy execution
 *
 * Key activity_record_t fields:
 *   correlation_id     — links to the callback ENTER record
 *   begin_ns / end_ns  — GPU-side timestamps (same clock as CPU on MI300A)
 *   device_id          — GPU device index
 *   queue_id           — HIP stream handle (numeric)
 *   kernel_name        — (DISPATCH only) mangled kernel name string
 *   bytes              — (COPY only) bytes transferred
 * ================================================================ */
static void activity_flush_cb(const char *begin, const char *end, void *arg) {
    const activity_record_t *rec = (const activity_record_t *)begin;

    while ((const char *)rec < end) {

        if (rec->domain == ACTIVITY_DOMAIN_HIP_OPS) {
            uint64_t duration_ns = rec->end_ns - rec->begin_ns;

            if (rec->op == HIP_OP_ID_DISPATCH) {
                /* ---- kernel activity ---- */
#if defined(WITH_LTTNG) && !defined(__HIP_DEVICE_COMPILE__)
                lttng_ust_tracepoint(lttng_roctracer, hipKernelActivity,
                                     rec->correlation_id,
                                     rec->begin_ns, rec->end_ns,
                                     rec->device_id, (uint32_t)rec->queue_id,
                                     rec->kernel_name ? rec->kernel_name : "");
#else
                printf("[ROCTracer ACTIVITY] kernel  corr=%-4lu  "
                       "begin=%lu  end=%lu  dur=%lu ns  "
                       "device=%d  queue=%lu  name=%s\n",
                       (unsigned long)rec->correlation_id,
                       (unsigned long)rec->begin_ns,
                       (unsigned long)rec->end_ns,
                       (unsigned long)duration_ns,
                       rec->device_id,
                       (unsigned long)rec->queue_id,
                       rec->kernel_name ? rec->kernel_name : "(unknown)");
#endif

            } else if (rec->op == HIP_OP_ID_COPY) {
                /* ---- memcpy activity ---- */
                /* NOTE: on MI300A (unified HBM), HtoD/DtoH copies are
                 * intra-HBM moves; begin_ns ≈ end_ns is expected.     */
#if defined(WITH_LTTNG) && !defined(__HIP_DEVICE_COMPILE__)
                lttng_ust_tracepoint(lttng_roctracer, hipMemcpyActivity,
                                     rec->correlation_id,
                                     rec->begin_ns, rec->end_ns,
                                     rec->device_id, (uint32_t)rec->queue_id,
                                     (uint64_t)rec->bytes);
#else
                printf("[ROCTracer ACTIVITY] memcpy  corr=%-4lu  "
                       "begin=%lu  end=%lu  dur=%lu ns  "
                       "device=%d  bytes=%lu\n",
                       (unsigned long)rec->correlation_id,
                       (unsigned long)rec->begin_ns,
                       (unsigned long)rec->end_ns,
                       (unsigned long)duration_ns,
                       rec->device_id,
                       (unsigned long)rec->bytes);
#endif
            }
        }

        /* Advance to next record (records are variable-length) */
        roctracer_next_record(rec, &rec);
    }
}

static roctracer_pool_t *activity_pool = NULL;

/* ================================================================
 * Callback API — called synchronously on ENTER and EXIT
 *
 * domain == ACTIVITY_DOMAIN_HIP_API for all HIP runtime calls.
 * cid    == HIP_API_ID_hip*  (unique integer per function)
 * api_data->phase: ACTIVITY_API_PHASE_ENTER or ACTIVITY_API_PHASE_EXIT
 * api_data->args.*: function-specific argument structs (see hip_prof_str.h)
 * api_data->correlation_id: links ENTER/EXIT and the activity record
 * ================================================================ */
static void hip_api_callback(uint32_t domain, uint32_t cid,
                              const void *callback_data, void *arg) {
    if (domain != ACTIVITY_DOMAIN_HIP_API)
        return;

    const hip_api_data_t *api_data = (const hip_api_data_t *)callback_data;
    uint64_t corr = api_data->correlation_id;

    /* ---- hipLaunchKernel ---- */
    if (cid == HIP_API_ID_hipLaunchKernel) {
        if (api_data->phase == ACTIVITY_API_PHASE_ENTER) {
            const void *faddr = api_data->args.hipLaunchKernel.function_address;
            dim3 grid  = api_data->args.hipLaunchKernel.numBlocks;
            dim3 block = api_data->args.hipLaunchKernel.dimBlocks;
            /* hipKernelNameRefByPtr resolves the function pointer → name */
            const char *kname = hipKernelNameRefByPtr(
                faddr, api_data->args.hipLaunchKernel.stream);

#if defined(WITH_LTTNG) && !defined(__HIP_DEVICE_COMPILE__)
            lttng_ust_tracepoint(lttng_roctracer, hipKernelLaunch_begin,
                                 corr, faddr,
                                 grid.x, grid.y, grid.z,
                                 block.x, block.y, block.z,
                                 kname ? kname : "");
#else
            printf("[ROCTracer CB] hipLaunchKernel ENTER  corr=%-4lu  "
                   "func=%p  grid=(%u,%u,%u)  block=(%u,%u,%u)  name=%s\n",
                   (unsigned long)corr, faddr,
                   grid.x, grid.y, grid.z,
                   block.x, block.y, block.z,
                   kname ? kname : "(unknown)");
#endif

        } else { /* EXIT */
#if defined(WITH_LTTNG) && !defined(__HIP_DEVICE_COMPILE__)
            lttng_ust_tracepoint(lttng_roctracer, hipKernelLaunch_end, corr);
#else
            printf("[ROCTracer CB] hipLaunchKernel EXIT   corr=%lu\n",
                   (unsigned long)corr);
#endif
        }
        return;
    }

    /* ---- hipMemcpy ---- */
    if (cid == HIP_API_ID_hipMemcpy) {
        if (api_data->phase == ACTIVITY_API_PHASE_ENTER) {
            void       *dst   = api_data->args.hipMemcpy.dst;
            const void *src   = api_data->args.hipMemcpy.src;
            size_t      bytes = api_data->args.hipMemcpy.sizeBytes;
            int         kind  = (int)api_data->args.hipMemcpy.kind;

#if defined(WITH_LTTNG) && !defined(__HIP_DEVICE_COMPILE__)
            lttng_ust_tracepoint(lttng_roctracer, hipMemcpy_begin,
                                 corr, dst, src, (uint64_t)bytes, kind);
#else
            printf("[ROCTracer CB] hipMemcpy ENTER  corr=%-4lu  "
                   "dst=%p  src=%p  bytes=%zu  kind=%s\n",
                   (unsigned long)corr, dst, src, bytes,
                   memcpy_kind_str(kind));
#endif

        } else { /* EXIT */
#if defined(WITH_LTTNG) && !defined(__HIP_DEVICE_COMPILE__)
            lttng_ust_tracepoint(lttng_roctracer, hipMemcpy_end, corr);
#else
            printf("[ROCTracer CB] hipMemcpy EXIT   corr=%lu\n",
                   (unsigned long)corr);
#endif
        }
        return;
    }

    /* ---- hipDeviceSynchronize ---- */
    if (cid == HIP_API_ID_hipDeviceSynchronize) {
        if (api_data->phase == ACTIVITY_API_PHASE_ENTER) {
#if defined(WITH_LTTNG) && !defined(__HIP_DEVICE_COMPILE__)
            lttng_ust_tracepoint(lttng_roctracer, hipDeviceSync_begin, corr);
#else
            printf("[ROCTracer CB] hipDeviceSynchronize ENTER  corr=%lu\n",
                   (unsigned long)corr);
#endif
        } else {
#if defined(WITH_LTTNG) && !defined(__HIP_DEVICE_COMPILE__)
            lttng_ust_tracepoint(lttng_roctracer, hipDeviceSync_end, corr);
#else
            printf("[ROCTracer CB] hipDeviceSynchronize EXIT   corr=%lu\n",
                   (unsigned long)corr);
#endif
        }
        return;
    }
}

/* ================================================================
 * Init / Fini
 * ================================================================ */
void ROCTRACER_Init(void) {
    /* 1. Register the callback for all HIP API calls */
    roctracer_enable_domain_callback(ACTIVITY_DOMAIN_HIP_API,
                                     hip_api_callback, NULL);

    /* 2. Open an activity pool for async GPU-side records.
     *    buffer_size: 2 MB should hold thousands of records.
     *    buffer_callback_fun: called when the pool is flushed. */
    roctracer_properties_t props;
    memset(&props, 0, sizeof(props));
    props.buffer_size         = 0x200000;   /* 2 MB */
    props.buffer_callback_fun = activity_flush_cb;
    roctracer_open_pool_expl(&props, &activity_pool);

    /* 3. Enable async activity recording for HIP GPU-side operations.
     *    Use the _expl variant to route records into OUR pool, not the
     *    default pool.  roctracer_enable_domain_activity() (no pool arg)
     *    would silently send records to a default pool that has no callback. */
    roctracer_enable_domain_activity_expl(ACTIVITY_DOMAIN_HIP_OPS, activity_pool);

    printf("[ROCTracer] Initialized%s\n",
#if defined(WITH_LTTNG) && !defined(__HIP_DEVICE_COMPILE__)
           " (LTTng UST tracing enabled)"
#else
           " (printf tracing)"
#endif
    );
}

void ROCTRACER_Fini(void) {
    /* Flush any remaining activity records before closing */
    if (activity_pool) {
        roctracer_flush_activity_expl(activity_pool);
        roctracer_disable_domain_activity(ACTIVITY_DOMAIN_HIP_OPS);
        roctracer_close_pool_expl(activity_pool);
        activity_pool = NULL;
    }
    roctracer_disable_domain_callback(ACTIVITY_DOMAIN_HIP_API);
    printf("[ROCTracer] Finalized\n");
}

#endif /* WITH_ROCTRACER */
