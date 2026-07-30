#include "pinsight.h"
#include "pinsight_control_thread.h"
#include "trace_config.h"
#include <hip/hip_runtime.h>
#include <roctracer/roctracer.h>
#include <roctracer/roctracer_hip.h>
#include <stdio.h>
#include <stdlib.h>
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
#define HIP_EVENT_MALLOC              20
#define HIP_EVENT_FREE               21
#define HIP_EVENT_MEMCPY_HTOD         25
#define HIP_EVENT_MEMCPY_DTOH         26
#define HIP_EVENT_MEMCPY_DTOD         27
#define HIP_EVENT_MEMCPY_HTOH         28
#define HIP_EVENT_MEMCPY_ASYNC        29
#define HIP_EVENT_KERNEL_LAUNCH       32

/* ================================================================
 * Intercepted HIP API operations — SINGLE SOURCE OF TRUTH.
 *
 * Callbacks are registered PER-OP (roctracer_enable_op_callback), not
 * domain-wide: with domain-wide registration the callback fired for EVERY
 * HIP API call the application makes (~450 op kinds, twice per call for
 * enter+exit) only to be software-rejected here — measured at +15% solve
 * time on AMG2023 16-node/64-GPU (2026-07-20 attribution experiment, where
 * MONITORING-with-zero-emission still cost +14.7%). Per-op registration
 * makes unlisted ops take the runtime's no-subscriber fast path: they never
 * enter PInsight at all. The fast-reject switch in hip_api_callback is kept
 * as defense-in-depth.
 *
 * Adding a new op: add one X(...) line here AND a handler block in
 * hip_api_callback. The cid array, registration helpers, and the
 * fast-reject switch all derive from this list.
 * ================================================================ */
#define PINSIGHT_HIP_API_OPS(X) \
    X(hipLaunchKernel)          \
    X(hipMemcpy)                \
    X(hipMemcpyAsync)           \
    X(hipMalloc)                \
    X(hipFree)                  \
    X(hipDeviceSynchronize)     \
    X(hipStreamSynchronize)     \
    X(hipDeviceReset)           \
    X(hipSetDevice)

static const uint32_t hip_api_op_cids[] = {
#define PINSIGHT_HIP_API_OP_CID(op) HIP_API_ID_##op,
    PINSIGHT_HIP_API_OPS(PINSIGHT_HIP_API_OP_CID)
#undef PINSIGHT_HIP_API_OP_CID
};
#define NUM_HIP_API_OPS \
    (sizeof(hip_api_op_cids) / sizeof(hip_api_op_cids[0]))

static void hip_api_callback(uint32_t domain, uint32_t cid,
                             const void *callback_data, void *arg);

static void hip_api_callbacks_enable(void) {
    for (size_t i = 0; i < NUM_HIP_API_OPS; i++) {
        if (roctracer_enable_op_callback(ACTIVITY_DOMAIN_HIP_API,
                                         hip_api_op_cids[i], hip_api_callback,
                                         NULL) != ROCTRACER_STATUS_SUCCESS)
            fprintf(stderr,
                    "PInsight: roctracer_enable_op_callback(cid=%u) failed: %s\n",
                    hip_api_op_cids[i], roctracer_error_string());
    }
}

static void hip_api_callbacks_disable(void) {
    for (size_t i = 0; i < NUM_HIP_API_OPS; i++)
        roctracer_disable_op_callback(ACTIVITY_DOMAIN_HIP_API,
                                      hip_api_op_cids[i]);
}

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
 *   OpenMP: 0+, CUDA: 2000+, Python: 3000+, HIP: 4000+
 * Plain int + __atomic builtins: clang rejects __atomic_* on _Atomic-qualified
 * objects (GCC accepts both). */
static int hip_thread_id_counter = 4000;

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
 *
 * hipGetDevice() reports the index RELATIVE TO this process's own
 * visible-device list (as masked by ROCR_VISIBLE_DEVICES /
 * HIP_VISIBLE_DEVICES), which is always 0 when a launcher restricts each
 * process to exactly one GPU (e.g. one-GPU-per-rank binding) — regardless
 * of which physical GPU that is. To record the actual physical device, add
 * the first index named in ROCR_VISIBLE_DEVICES (or HIP_VISIBLE_DEVICES) as
 * an offset; hipGetDevice()'s relative index is exactly an index into that
 * visible-device list, so this generalizes correctly. Falls back to the
 * plain hipGetDevice() value when neither variable is set (no masking).
 * ================================================================ */
static __thread int hip_tls_dev_cached = 0;
static __thread int hip_tls_devId = 0;

static inline int hip_visible_device_offset(void) {
    const char *v = getenv("ROCR_VISIBLE_DEVICES");
    if (!v || !*v) v = getenv("HIP_VISIBLE_DEVICES");
    if (!v || !*v) return 0;
    return atoi(v); /* first comma-separated token; atoi stops at ',' */
}

static inline int hip_get_cached_device(void) {
    if (__builtin_expect(hip_tls_dev_cached, 1))
        return hip_tls_devId;
    int dev;
    hipGetDevice(&dev);
    hip_tls_devId = dev + hip_visible_device_offset();
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
#ifdef PINSIGHT_LAYOUT_PAD
/* Layout-perturbation probe (2026-07-24 bimodal-anomaly discriminator, design
 * doc §6.9 / code-memory OPEN ANOMALY): an inert, referenced .data pad that
 * shifts every subsequent global by PINSIGHT_LAYOUT_PAD bytes. Identical
 * source semantics; only binary layout differs between the two builds. */
__attribute__((used)) static char pinsight_layout_pad_hip[PINSIGHT_LAYOUT_PAD] = {1};
#endif
static roctracer_pool_t *activity_pool = NULL;

/* ================================================================
 * Runtime-live activity gate (Phase 2 — design doc §6.9).
 *
 * Whether THIS process collects the GPU activity pool, per the [HIP.default]
 * device_activity node-policy — evaluated LIVE (control thread) so it can
 * change within a run: config reload flips the VALUE (off/on), and
 * rotate_per_node derives the collector from the node-wide monotonic clock.
 * The ELECTION / selection METHOD stays pinned per-run (§4.5): the first
 * anyone/leader/rotate value seen pins the method (+ rotate period); a later
 * method change is warned once and ignored. Host callbacks are NEVER gated by
 * any of this — only the activity pool — so host tracing stays on all ranks.
 * ================================================================ */
static pinsight_nodepolicy_t hip_pinned_method; /* valid iff hip_method_pinned */
static int hip_method_pinned = 0;
static int hip_pinned_rotate_ms = 0;
static int hip_method_warned = 0;

static inline int hip_nodepolicy_idx(void) {
    static int idx = -2; /* -2 = not looked up yet */
    if (idx == -2)
        idx = pinsight_get_nodepolicy_index(HIP_domain_index, "device_activity");
    return idx;
}

static inline uint64_t hip_mono_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)(ts.tv_nsec / 1000000L);
}

/* Live per-rank collect decision:
 *   (trace_mode == TRACING) AND (device_activity selects this rank NOW). */
static int hip_should_collect_now(void) {
    if (domain_default_trace_config[HIP_domain_index].mode !=
        PINSIGHT_DOMAIN_TRACING)
        return 0; /* master gate: activity capture only in TRACING */
    int idx = hip_nodepolicy_idx();
    if (idx < 0)
        return 0;
    pinsight_nodepolicy_val_t v =
        domain_default_trace_config[HIP_domain_index].nodepolicy[idx];
    switch (v.policy) {
    case PINSIGHT_NODEPOLICY_OFF:
        return 0;
    case PINSIGHT_NODEPOLICY_ON:
        return 1;
    default: /* ANYONE / LEADER / ROTATE — method-bearing values */
        if (!hip_method_pinned) {
            hip_pinned_method = v.policy;
            hip_pinned_rotate_ms = (v.param > 0) ? v.param : 1000;
            hip_method_pinned = 1;
        } else if (v.policy != hip_pinned_method && !hip_method_warned) {
            fprintf(stderr,
                    "PInsight: device_activity selection method changed at "
                    "runtime (%s -> %s) — IGNORED, method is pinned per-run\n",
                    pinsight_nodepolicy_str(hip_pinned_method),
                    pinsight_nodepolicy_str(v.policy));
            hip_method_warned = 1;
        }
        if (hip_pinned_method == PINSIGHT_NODEPOLICY_ROTATE_PER_NODE) {
            int lr = pinsight_local_rank();
            int n = pinsight_ranks_per_node();
            if (lr < 0 || n <= 0) /* unknown topology → degrade to leader */
                return pinsight_node_role("hip_activity",
                                          PINSIGHT_NODEPOLICY_LEADER_PER_NODE);
            /* Node-wide clock → all local ranks agree with zero IPC. */
            return (int)((hip_mono_ms() / (uint64_t)hip_pinned_rotate_ms) %
                         (uint64_t)n) == lr;
        }
        return pinsight_node_role("hip_activity", hip_pinned_method);
    }
}

/* Rotate period for the control thread's boundary wake: >0 (ms) iff the live
 * policy is rotate and the domain is TRACING with known topology; else 0. */
int pinsight_control_hip_rotate_period_ms(void) {
    if (domain_default_trace_config[HIP_domain_index].mode !=
        PINSIGHT_DOMAIN_TRACING)
        return 0;
    int idx = hip_nodepolicy_idx();
    if (idx < 0)
        return 0;
    pinsight_nodepolicy_val_t v =
        domain_default_trace_config[HIP_domain_index].nodepolicy[idx];
    pinsight_nodepolicy_t m = hip_method_pinned ? hip_pinned_method : v.policy;
    if (m != PINSIGHT_NODEPOLICY_ROTATE_PER_NODE)
        return 0;
    if (v.policy == PINSIGHT_NODEPOLICY_OFF)
        return 0; /* rotation paused by a live off */
    if (pinsight_local_rank() < 0 || pinsight_ranks_per_node() <= 0)
        return 0; /* degraded to leader — no tick needed */
    return hip_method_pinned ? hip_pinned_rotate_ms
                             : ((v.param > 0) ? v.param : 1000);
}

/* Gate for emitting activity records on flush.  NOT the live domain mode: the
 * pool batches records and is flushed on buffer-full or at transitions, so the
 * mode at flush time may differ from the mode when the records were captured.
 * Instead this flag tracks "the batch currently in the pool should be emitted",
 * and pinsight_control_hip_apply_mode() flushes at every MONITORING<->TRACING
 * boundary *before* flipping it — so a batch captured under TRACING is always
 * dumped with emit==1, and is never stranded for a later MONITORING flush to
 * drop.  Set/cleared only by the control thread (and Init); read here. */
static volatile int hip_activity_emit = 0;

static void hip_activity_callback(const char *begin, const char *end,
                                  void *arg) {
    /* Suppress in OFF/STANDBY/MONITORING (without this, MONITORING leaked every
     * GPU activity record). Only async ops still in-flight at a TRACING->lower
     * switch can leak/drop — best-effort by design. */
    if (!hip_activity_emit)
        return;
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

    /* 3. Fast reject (defense-in-depth): registration is per-op, so only
     * the PINSIGHT_HIP_API_OPS cids should ever arrive here — but keep the
     * check in case a roctracer version routes unexpected cids. Generated
     * from the same single-source op table as the registration. */
    switch (cid) {
#define PINSIGHT_HIP_API_OP_CASE(op) case HIP_API_ID_##op:
    PINSIGHT_HIP_API_OPS(PINSIGHT_HIP_API_OP_CASE)
#undef PINSIGHT_HIP_API_OP_CASE
        break;
    default:
        return;
    }

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
        /* Debug knob: PINSIGHT_HIP_NO_KERNELNAME=1 skips the roctracer name
         * lookup (suspected abort source inside the ROCm>=7 deprecated-
         * roctracer shim for some apps). */
        static int no_name_lookup = -1;
        if (no_name_lookup < 0)
            no_name_lookup = getenv("PINSIGHT_HIP_NO_KERNELNAME") != NULL;
        const char *kernelName = no_name_lookup ? NULL : hipKernelNameRefByPtr(
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

    /* ========== hipMalloc ========== */
    if (cid == HIP_API_ID_hipMalloc) {
        const void *codeptr = (const void *)domain_info_table[HIP_domain_index]
                                  .event_table[HIP_EVENT_MALLOC].name;

        if (api_data->phase == ACTIVITY_API_PHASE_ENTER) {
            lexgion_record_t *record =
                lexgion_begin(ROCL_LEXGION, HIP_EVENT_MALLOC, codeptr);
            lexgion_t *lgp = record->lgp;

            if (lgp->name_resolved_gen != trace_config_change_counter) {
                lgp->name = domain_info_table[HIP_domain_index]
                                .event_table[HIP_EVENT_MALLOC].name;
                lgp->filename_hint    = NULL;
                lgp->name_resolved_gen = trace_config_change_counter;
                lgp->trace_config_change_counter = (unsigned int)-1;
            }

            if (PINSIGHT_SHOULD_TRACE(HIP_domain_index))
                lexgion_set_top_trace_bit_domain_event(
                    lgp, HIP_domain_index, HIP_EVENT_MALLOC);

            if (PINSIGHT_SHOULD_TRACE(HIP_domain_index) && lgp->trace_bit) {
                size_t size = api_data->args.hipMalloc.size;
                uint64_t ts = hip_fast_timestamp();
#ifdef PINSIGHT_BACKTRACE
                retrieve_backtrace();
#endif
                lttng_ust_tracepoint(roctracer_pinsight_lttng_ust,
                                     hipMalloc_begin,
                                     devId, api_data->correlation_id, ts,
                                     codeptr, lgp->name, size);
            }
        } else if (api_data->phase == ACTIVITY_API_PHASE_EXIT) {
            lexgion_t *lgp = lexgion_end(NULL);
            if (lgp && PINSIGHT_SHOULD_TRACE(HIP_domain_index) && lgp->trace_bit) {
                /* At EXIT the device address has been written into *ptr */
                void *dev_ptr = api_data->args.hipMalloc.ptr
                                    ? *(api_data->args.hipMalloc.ptr) : NULL;
                int rv      = 0; /* hip_api_data_t has no retval field */
                uint64_t ts = hip_fast_timestamp();
                lttng_ust_tracepoint(roctracer_pinsight_lttng_ust,
                                     hipMalloc_end,
                                     devId, api_data->correlation_id, ts,
                                     codeptr, lgp->name, dev_ptr, rv);
                lexgion_post_trace_update(lgp);
            }
        }
        return;
    }

    /* ========== hipFree ========== */
    if (cid == HIP_API_ID_hipFree) {
        const void *codeptr = (const void *)domain_info_table[HIP_domain_index]
                                  .event_table[HIP_EVENT_FREE].name;

        if (api_data->phase == ACTIVITY_API_PHASE_ENTER) {
            lexgion_record_t *record =
                lexgion_begin(ROCL_LEXGION, HIP_EVENT_FREE, codeptr);
            lexgion_t *lgp = record->lgp;

            if (lgp->name_resolved_gen != trace_config_change_counter) {
                lgp->name = domain_info_table[HIP_domain_index]
                                .event_table[HIP_EVENT_FREE].name;
                lgp->filename_hint    = NULL;
                lgp->name_resolved_gen = trace_config_change_counter;
                lgp->trace_config_change_counter = (unsigned int)-1;
            }

            if (PINSIGHT_SHOULD_TRACE(HIP_domain_index))
                lexgion_set_top_trace_bit_domain_event(
                    lgp, HIP_domain_index, HIP_EVENT_FREE);

            if (PINSIGHT_SHOULD_TRACE(HIP_domain_index) && lgp->trace_bit) {
                void *dev_ptr = api_data->args.hipFree.ptr;
                uint64_t ts   = hip_fast_timestamp();
#ifdef PINSIGHT_BACKTRACE
                retrieve_backtrace();
#endif
                lttng_ust_tracepoint(roctracer_pinsight_lttng_ust,
                                     hipFree_begin,
                                     devId, api_data->correlation_id, ts,
                                     codeptr, lgp->name, dev_ptr);
            }
        } else if (api_data->phase == ACTIVITY_API_PHASE_EXIT) {
            lexgion_t *lgp = lexgion_end(NULL);
            if (lgp && PINSIGHT_SHOULD_TRACE(HIP_domain_index) && lgp->trace_bit) {
                int rv      = 0; /* hip_api_data_t has no retval field */
                uint64_t ts = hip_fast_timestamp();
                lttng_ust_tracepoint(roctracer_pinsight_lttng_ust,
                                     hipFree_end,
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

/* ================================================================
 * Activity apply driver (Phase 2 §6.9.2). Control-thread-owned (plus the
 * pre-thread Init call). Idempotent: compares the live decision against the
 * last applied state and does a FULL teardown / setup on change:
 *   off: flush (emits the on-window tail while emit==1) → clear emit →
 *        disable_domain_activity (stops the per-op HSA instrumentation) →
 *        close_pool (frees the buffer + DEREGISTERS the buffer callback).
 *   on : open_pool (registers callback, allocs) → enable → set emit.
 * No dormant pool remains while not collecting.
 * ================================================================ */
static int hip_collect_state = 0; /* last applied collect state */

/* Diagnostic: PINSIGHT_DEBUG_ACTIVITY=1 logs every activity-machinery action
 * with monotonic timestamp + the (normally unchecked) roctracer return codes. */
static int hip_dbg_activity(void) {
    static int on = -1;
    if (on < 0) { const char *v = getenv("PINSIGHT_DEBUG_ACTIVITY"); on = (v && *v && *v != '0'); }
    return on;
}
#define HIP_DBG(...) do { if (hip_dbg_activity()) { \
    struct timespec _ts; clock_gettime(CLOCK_MONOTONIC, &_ts); \
    fprintf(stderr, "PINSIGHT_DBG t=%ld.%03ld pid=%d ", (long)_ts.tv_sec, \
            _ts.tv_nsec/1000000L, getpid()); \
    fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); } } while (0)

void pinsight_control_hip_apply_collect_state(void) {
    if (hip_permanently_off)
        return;
    int s = hip_should_collect_now();
    if (s == hip_collect_state)
        return;
    if (s) {
        int rc_open = -999, rc_en;
        if (!activity_pool) {
            roctracer_properties_t props;
            memset(&props, 0, sizeof(props));
            props.buffer_size         = 0x200000; /* 2 MB (8 MB probe: flush was not the bottleneck) */
            props.buffer_callback_fun = hip_activity_callback;
            rc_open = roctracer_open_pool_expl(&props, &activity_pool);
        }
        rc_en = roctracer_enable_domain_activity_expl(ACTIVITY_DOMAIN_HIP_OPS,
                                                      activity_pool);
        hip_activity_emit = 1;
        HIP_DBG("apply 0->1 open_rc=%d enable_rc=%d pool=%p", rc_open, rc_en,
                (void *)activity_pool);
    } else {
        int rc_fl = -999, rc_dis, rc_cl = -999;
        if (activity_pool)
            rc_fl = roctracer_flush_activity_expl(activity_pool); /* emit==1 → tail emitted */
        hip_activity_emit = 0;
        rc_dis = roctracer_disable_domain_activity(ACTIVITY_DOMAIN_HIP_OPS);
        if (activity_pool) {
            rc_cl = roctracer_close_pool_expl(activity_pool);
            activity_pool = NULL;
        }
        HIP_DBG("apply 1->0 flush_rc=%d disable_rc=%d close_rc=%d", rc_fl,
                rc_dis, rc_cl);
    }
    hip_collect_state = s;
}

/* Called by the control thread for runtime mode transitions.
 * Precondition: pinsight_hip_init() has run, so last_mode is never NONE.
 * Lifecycle: the host API callbacks are tied to ACTIVE (MONITORING or
 * TRACING); the activity pool + collection are handled entirely by the live
 * gate (hip_should_collect_now → apply_collect_state): collection exists only
 * while TRACING *and* this rank is the selected collector, with full pool
 * teardown otherwise (Phase 2 §6.9 — no dormant pool in MONITORING). */
void pinsight_control_hip_apply_mode(void) {
    if (hip_permanently_off)
        return;

    pinsight_domain_mode_t mode =
        domain_default_trace_config[HIP_domain_index].mode;
    pinsight_domain_mode_t last =
        domain_default_trace_config[HIP_domain_index].last_mode;

    if (mode == last)
        return;

    /* Activity first (flush/close while the runtime is still fully alive;
     * the gate reads the already-committed new mode). */
    pinsight_control_hip_apply_collect_state();

    if (mode == PINSIGHT_DOMAIN_OFF || mode == PINSIGHT_DOMAIN_STANDBY) {
        if (PINSIGHT_DOMAIN_ACTIVE(last))
            hip_api_callbacks_disable();
        /* STANDBY→OFF or STANDBY→STANDBY: nothing to do */
        if (mode == PINSIGHT_DOMAIN_OFF) {
            hip_permanently_off = 1;
            fprintf(stderr, "PInsight: HIP domain permanently OFF\n");
        }
    } else if (!PINSIGHT_DOMAIN_ACTIVE(last)) {
        /* STANDBY → MONITORING/TRACING: enable the host API callbacks. */
        hip_api_callbacks_enable();
    }
    /* MONITORING↔TRACING: host callbacks already on; activity handled above. */
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
            hip_api_callbacks_enable();
            /* Activity pool + collection: the live gate opens/enables iff
             * TRACING and this rank is the selected collector (device_activity
             * node-policy). Host tracing stays on regardless. */
            pinsight_control_hip_apply_collect_state();
            HIP_DBG("init mode=%d should_collect=%d collect_state=%d",
                    (int)mode, hip_should_collect_now(), hip_collect_state);
        }
        /* STANDBY: callback and pool stay unregistered until first active transition */
    }
    domain_default_trace_config[HIP_domain_index].last_mode = mode;
}

void LTTNG_ROCTRACER_Fini(void) {
    if (hip_permanently_off)
        return;
    /* Final flush runs with hip_activity_emit still reflecting the last mode, so
     * records captured while TRACING are emitted.  Collection is only on if we
     * ended in TRACING (hip_activity_emit==1); disable it only then. */
    if (activity_pool) {
        roctracer_flush_activity_expl(activity_pool);
        if (hip_activity_emit)
            roctracer_disable_domain_activity(ACTIVITY_DOMAIN_HIP_OPS);
        roctracer_close_pool_expl(activity_pool);
        activity_pool = NULL;
    }
    hip_api_callbacks_disable();
    hip_activity_emit = 0;
    hip_permanently_off = 1;
}
