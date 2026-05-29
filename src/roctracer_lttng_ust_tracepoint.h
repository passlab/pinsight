#undef LTTNG_UST_TRACEPOINT_PROVIDER
#define LTTNG_UST_TRACEPOINT_PROVIDER roctracer_pinsight_lttng_ust

#undef LTTNG_UST_TRACEPOINT_INCLUDE
#define LTTNG_UST_TRACEPOINT_INCLUDE "./roctracer_lttng_ust_tracepoint.h"

#if !defined(_ROCTRACER_LTTNG_UST_TRACEPOINT_H_) || defined(LTTNG_UST_TRACEPOINT_HEADER_MULTI_READ)
#define _ROCTRACER_LTTNG_UST_TRACEPOINT_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <lttng/tracepoint.h>
#include <stdint.h>

#ifndef _ROCTRACER_LTTNG_UST_TRACEPOINT_H_DECLARE_ONCE_
#define _ROCTRACER_LTTNG_UST_TRACEPOINT_H_DECLARE_ONCE_
#ifdef PINSIGHT_MPI
extern int mpirank;
#include "common_tp_fields_global_lttng_ust_tracepoint.h"
#endif
#ifdef PINSIGHT_OPENMP
extern __thread int global_thread_num;
extern __thread int omp_team_num;
extern __thread int omp_thread_num;
#endif
#ifdef PINSIGHT_BACKTRACE
#include <backtrace.h>
#else
#define LTTNG_UST_TP_FIELDS_BACKTRACE
#endif

struct hip_dimension_t {
    unsigned int gridx;
    unsigned int gridy;
    unsigned int gridz;
    unsigned int blockx;
    unsigned int blocky;
    unsigned int blockz;
};

#endif /* _ROCTRACER_LTTNG_UST_TRACEPOINT_H_DECLARE_ONCE_ */

/* Common args carried by every HIP callback tracepoint */
#define COMMON_HIP_ARG                                                         \
    unsigned int,       devId,                                                 \
    uint64_t,           correlation_id,                                        \
    uint64_t,           hip_timeStamp,                                         \
    const void *,       codeptr,                                               \
    const char *,       func_name

#if defined(PINSIGHT_MPI) && defined(PINSIGHT_OPENMP)
#define COMMON_LTTNG_UST_TP_FIELDS_HIP                                        \
    COMMON_LTTNG_UST_TP_FIELDS_GLOBAL                                         \
    LTTNG_UST_TP_FIELDS_BACKTRACE                                              \
    lttng_ust_field_integer(unsigned int, mpirank,           mpirank)         \
    lttng_ust_field_integer(unsigned int, global_thread_num, global_thread_num)\
    lttng_ust_field_integer(unsigned int, omp_team_num,      omp_team_num)    \
    lttng_ust_field_integer(unsigned int, omp_thread_num,    omp_thread_num)  \
    lttng_ust_field_integer(unsigned int, devId,             devId)           \
    lttng_ust_field_integer(uint64_t,     correlation_id,    correlation_id)  \
    lttng_ust_field_integer(uint64_t,     hip_timeStamp,     hip_timeStamp)   \
    lttng_ust_field_integer_hex(unsigned long int, hip_codeptr, codeptr)      \
    lttng_ust_field_string(hip_func, func_name)
#elif defined(PINSIGHT_MPI) && !defined(PINSIGHT_OPENMP)
#define COMMON_LTTNG_UST_TP_FIELDS_HIP                                        \
    COMMON_LTTNG_UST_TP_FIELDS_GLOBAL                                         \
    LTTNG_UST_TP_FIELDS_BACKTRACE                                              \
    lttng_ust_field_integer(unsigned int, mpirank,        mpirank)            \
    lttng_ust_field_integer(unsigned int, devId,          devId)              \
    lttng_ust_field_integer(uint64_t,     correlation_id, correlation_id)     \
    lttng_ust_field_integer(uint64_t,     hip_timeStamp,  hip_timeStamp)      \
    lttng_ust_field_integer_hex(unsigned long int, hip_codeptr, codeptr)      \
    lttng_ust_field_string(hip_func, func_name)
#elif !defined(PINSIGHT_MPI) && defined(PINSIGHT_OPENMP)
#define COMMON_LTTNG_UST_TP_FIELDS_HIP                                        \
    LTTNG_UST_TP_FIELDS_BACKTRACE                                              \
    lttng_ust_field_integer(unsigned int, global_thread_num, global_thread_num)\
    lttng_ust_field_integer(unsigned int, omp_team_num,      omp_team_num)    \
    lttng_ust_field_integer(unsigned int, omp_thread_num,    omp_thread_num)  \
    lttng_ust_field_integer(unsigned int, devId,             devId)           \
    lttng_ust_field_integer(uint64_t,     correlation_id,    correlation_id)  \
    lttng_ust_field_integer(uint64_t,     hip_timeStamp,     hip_timeStamp)   \
    lttng_ust_field_integer_hex(unsigned long int, hip_codeptr, codeptr)      \
    lttng_ust_field_string(hip_func, func_name)
#else
#define COMMON_LTTNG_UST_TP_FIELDS_HIP                                        \
    LTTNG_UST_TP_FIELDS_BACKTRACE                                              \
    lttng_ust_field_integer(unsigned int, devId,          devId)              \
    lttng_ust_field_integer(uint64_t,     correlation_id, correlation_id)     \
    lttng_ust_field_integer(uint64_t,     hip_timeStamp,  hip_timeStamp)      \
    lttng_ust_field_integer_hex(unsigned long int, hip_codeptr, codeptr)      \
    lttng_ust_field_string(hip_func, func_name)
#endif

/* hipMemcpyKind enum values (mirrors hipMemcpyKind in HIP headers) */
LTTNG_UST_TRACEPOINT_ENUM(roctracer_pinsight_lttng_ust, hipMemcpyKind_enum,
    LTTNG_UST_TP_ENUM_VALUES(
        lttng_ust_field_enum_value("hipMemcpyHostToHost",     0)
        lttng_ust_field_enum_value("hipMemcpyHostToDevice",   1)
        lttng_ust_field_enum_value("hipMemcpyDeviceToHost",   2)
        lttng_ust_field_enum_value("hipMemcpyDeviceToDevice", 3)
        lttng_ust_field_enum_value("hipMemcpyDefault",        4)
    )
)

/* ── hipMemcpy (synchronous) ─────────────────────────────────────────── */
LTTNG_UST_TRACEPOINT_EVENT(
    roctracer_pinsight_lttng_ust,
    hipMemcpy_begin,
    LTTNG_UST_TP_ARGS(
        COMMON_HIP_ARG,
        void *,       dst,
        const void *, src,
        size_t,       count,
        unsigned int, kind
    ),
    LTTNG_UST_TP_FIELDS(
        COMMON_LTTNG_UST_TP_FIELDS_HIP
        lttng_ust_field_integer(unsigned long int, dst,   dst)
        lttng_ust_field_integer(unsigned long int, src,   src)
        lttng_ust_field_integer(unsigned int,      count, count)
        lttng_ust_field_enum(roctracer_pinsight_lttng_ust, hipMemcpyKind_enum,
                             int, hipMemcpyKind, kind)
    )
)

LTTNG_UST_TRACEPOINT_EVENT(
    roctracer_pinsight_lttng_ust,
    hipMemcpy_end,
    LTTNG_UST_TP_ARGS(
        COMMON_HIP_ARG,
        int, return_val
    ),
    LTTNG_UST_TP_FIELDS(
        COMMON_LTTNG_UST_TP_FIELDS_HIP
        lttng_ust_field_integer(int, return_val, return_val)
    )
)

/* ── hipMemcpyAsync ──────────────────────────────────────────────────── */
/* NOTE: LTTng allows max 10 args/fields; streamId omitted to stay under limit */
LTTNG_UST_TRACEPOINT_EVENT(
    roctracer_pinsight_lttng_ust,
    hipMemcpyAsync_begin,
    LTTNG_UST_TP_ARGS(
        COMMON_HIP_ARG,
        void *,       dst,
        const void *, src,
        size_t,       count,
        unsigned int, kind
    ),
    LTTNG_UST_TP_FIELDS(
        COMMON_LTTNG_UST_TP_FIELDS_HIP
        lttng_ust_field_integer(unsigned long int, dst,   dst)
        lttng_ust_field_integer(unsigned long int, src,   src)
        lttng_ust_field_integer(unsigned int,      count, count)
        lttng_ust_field_enum(roctracer_pinsight_lttng_ust, hipMemcpyKind_enum,
                             int, hipMemcpyKind, kind)
    )
)

LTTNG_UST_TRACEPOINT_EVENT(
    roctracer_pinsight_lttng_ust,
    hipMemcpyAsync_end,
    LTTNG_UST_TP_ARGS(
        COMMON_HIP_ARG,
        int, return_val
    ),
    LTTNG_UST_TP_FIELDS(
        COMMON_LTTNG_UST_TP_FIELDS_HIP
        lttng_ust_field_integer(int, return_val, return_val)
    )
)

/* ── hipLaunchKernel ─────────────────────────────────────────────────── */
LTTNG_UST_TRACEPOINT_EVENT(
    roctracer_pinsight_lttng_ust,
    hipKernelLaunch_begin,
    LTTNG_UST_TP_ARGS(
        COMMON_HIP_ARG,
        struct hip_dimension_t *, dimension
    ),
    LTTNG_UST_TP_FIELDS(
        COMMON_LTTNG_UST_TP_FIELDS_HIP
        lttng_ust_field_integer(unsigned int, gridDimX,  dimension->gridx)
        lttng_ust_field_integer(unsigned int, gridDimY,  dimension->gridy)
        lttng_ust_field_integer(unsigned int, gridDimZ,  dimension->gridz)
        lttng_ust_field_integer(unsigned int, blockDimX, dimension->blockx)
        lttng_ust_field_integer(unsigned int, blockDimY, dimension->blocky)
        lttng_ust_field_integer(unsigned int, blockDimZ, dimension->blockz)
    )
)

LTTNG_UST_TRACEPOINT_EVENT(
    roctracer_pinsight_lttng_ust,
    hipKernelLaunch_end,
    LTTNG_UST_TP_ARGS(
        COMMON_HIP_ARG
    ),
    LTTNG_UST_TP_FIELDS(
        COMMON_LTTNG_UST_TP_FIELDS_HIP
    )
)

/* ── hipDeviceSynchronize ────────────────────────────────────────────── */
LTTNG_UST_TRACEPOINT_EVENT(
    roctracer_pinsight_lttng_ust,
    hipDeviceSync_begin,
    LTTNG_UST_TP_ARGS(
        COMMON_HIP_ARG
    ),
    LTTNG_UST_TP_FIELDS(
        COMMON_LTTNG_UST_TP_FIELDS_HIP
    )
)

LTTNG_UST_TRACEPOINT_EVENT(
    roctracer_pinsight_lttng_ust,
    hipDeviceSync_end,
    LTTNG_UST_TP_ARGS(
        COMMON_HIP_ARG,
        int, return_val
    ),
    LTTNG_UST_TP_FIELDS(
        COMMON_LTTNG_UST_TP_FIELDS_HIP
        lttng_ust_field_integer(int, return_val, return_val)
    )
)

/* ── hipStreamSynchronize ────────────────────────────────────────────── */
LTTNG_UST_TRACEPOINT_EVENT(
    roctracer_pinsight_lttng_ust,
    hipStreamSync_begin,
    LTTNG_UST_TP_ARGS(
        COMMON_HIP_ARG,
        unsigned int, streamId
    ),
    LTTNG_UST_TP_FIELDS(
        COMMON_LTTNG_UST_TP_FIELDS_HIP
        lttng_ust_field_integer(unsigned int, streamId, streamId)
    )
)

LTTNG_UST_TRACEPOINT_EVENT(
    roctracer_pinsight_lttng_ust,
    hipStreamSync_end,
    LTTNG_UST_TP_ARGS(
        COMMON_HIP_ARG,
        int, return_val
    ),
    LTTNG_UST_TP_FIELDS(
        COMMON_LTTNG_UST_TP_FIELDS_HIP
        lttng_ust_field_integer(int, return_val, return_val)
    )
)

/* ── Activity API tracepoints ────────────────────────────────────────── */
/*
 * Fired from the ROCTracer activity pool flush callback.
 * begin_ns / end_ns are GPU-side timestamps (CLOCK_MONOTONIC on MI300A).
 * Use correlation_id to link to the corresponding callback tracepoint.
 *
 * On MI300A (unified HBM), hipMemcpyActivity records will have
 * begin_ns ≈ end_ns — no actual DMA transfer occurs.
 */

LTTNG_UST_TRACEPOINT_EVENT(
    roctracer_pinsight_lttng_ust,
    hip_clock_calibration,
    LTTNG_UST_TP_ARGS(
        uint64_t, clock_monotonic_ns,
        uint64_t, roctracer_timestamp_ns
    ),
    LTTNG_UST_TP_FIELDS(
        lttng_ust_field_integer(uint64_t, clock_monotonic_ns,    clock_monotonic_ns)
        lttng_ust_field_integer(uint64_t, roctracer_timestamp_ns, roctracer_timestamp_ns)
    )
)

LTTNG_UST_TRACEPOINT_EVENT(
    roctracer_pinsight_lttng_ust,
    hipMemcpyActivity,
    LTTNG_UST_TP_ARGS(
        uint32_t, devId,
        uint64_t, correlation_id,
        uint64_t, begin_ns,
        uint64_t, end_ns,
        uint64_t, bytes,
        int,      copyKind,
        uint32_t, queueId
    ),
    LTTNG_UST_TP_FIELDS(
        lttng_ust_field_integer(uint32_t, devId,          devId)
        lttng_ust_field_integer(uint64_t, correlation_id, correlation_id)
        lttng_ust_field_integer(uint64_t, begin_ns,       begin_ns)
        lttng_ust_field_integer(uint64_t, end_ns,         end_ns)
        lttng_ust_field_integer(uint64_t, bytes,          bytes)
        lttng_ust_field_enum(roctracer_pinsight_lttng_ust, hipMemcpyKind_enum,
                             int, hipMemcpyKind, copyKind)
        lttng_ust_field_integer(uint32_t, queueId,        queueId)
    )
)

LTTNG_UST_TRACEPOINT_EVENT(
    roctracer_pinsight_lttng_ust,
    hipKernelActivity,
    LTTNG_UST_TP_ARGS(
        uint32_t,    devId,
        uint64_t,    correlation_id,
        uint64_t,    begin_ns,
        uint64_t,    end_ns,
        uint32_t,    queueId,
        const char*, kernelName
    ),
    LTTNG_UST_TP_FIELDS(
        lttng_ust_field_integer(uint32_t, devId,          devId)
        lttng_ust_field_integer(uint64_t, correlation_id, correlation_id)
        lttng_ust_field_integer(uint64_t, begin_ns,       begin_ns)
        lttng_ust_field_integer(uint64_t, end_ns,         end_ns)
        lttng_ust_field_integer(uint32_t, queueId,        queueId)
        lttng_ust_field_string(kernelName, kernelName)
    )
)

#ifdef __cplusplus
}
#endif

#endif /* _ROCTRACER_LTTNG_UST_TRACEPOINT_H_ */

#include <lttng/tracepoint-event.h>
