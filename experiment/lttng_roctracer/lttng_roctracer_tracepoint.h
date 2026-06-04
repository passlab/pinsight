/*
 * lttng_roctracer_tracepoint.h — LTTng UST tracepoint definitions
 *
 * Provider name: lttng_roctracer
 *
 * Tracepoints:
 *   Callback API (host-side, synchronous):
 *     hipKernelLaunch_begin / hipKernelLaunch_end
 *     hipMemcpy_begin        / hipMemcpy_end
 *     hipDeviceSync_begin    / hipDeviceSync_end
 *
 *   Activity API (GPU-side, asynchronous, from pool flush callback):
 *     hipKernelActivity  — actual GPU kernel execution window
 *     hipMemcpyActivity  — actual memcpy execution window
 *
 * Requires LTTng UST >= 2.13 (LTTNG_UST_TRACEPOINT_EVENT API).
 * Build with: -I$(HOME)/local/include  -L$(HOME)/local/lib -llttng-ust
 */

#undef LTTNG_UST_TRACEPOINT_PROVIDER
#define LTTNG_UST_TRACEPOINT_PROVIDER lttng_roctracer

#undef LTTNG_UST_TRACEPOINT_INCLUDE
#define LTTNG_UST_TRACEPOINT_INCLUDE "./lttng_roctracer_tracepoint.h"

#if !defined(_LTTNG_ROCTRACER_TRACEPOINT_H_) || \
    defined(LTTNG_UST_TRACEPOINT_HEADER_MULTI_READ)
#define _LTTNG_ROCTRACER_TRACEPOINT_H_

#include <lttng/tracepoint.h>
#include <stdint.h>

/* ================================================================
 * Callback API tracepoints  (host-side, synchronous)
 * ================================================================ */

/* hipLaunchKernel — ENTER: kernel function address + launch dimensions */
LTTNG_UST_TRACEPOINT_EVENT(
    lttng_roctracer,
    hipKernelLaunch_begin,
    LTTNG_UST_TP_ARGS(
        uint64_t,     correlation_id,
        const void *, func_addr,
        unsigned int, grid_x,
        unsigned int, grid_y,
        unsigned int, grid_z,
        unsigned int, block_x,
        unsigned int, block_y,
        unsigned int, block_z,
        const char *, kernel_name
    ),
    LTTNG_UST_TP_FIELDS(
        lttng_ust_field_integer(uint64_t,      correlation_id, correlation_id)
        lttng_ust_field_integer_hex(unsigned long, func_addr, (unsigned long)(func_addr))
        lttng_ust_field_integer(unsigned int,  grid_x,  grid_x)
        lttng_ust_field_integer(unsigned int,  grid_y,  grid_y)
        lttng_ust_field_integer(unsigned int,  grid_z,  grid_z)
        lttng_ust_field_integer(unsigned int,  block_x, block_x)
        lttng_ust_field_integer(unsigned int,  block_y, block_y)
        lttng_ust_field_integer(unsigned int,  block_z, block_z)
        lttng_ust_field_string(kernel_name,    kernel_name)
    )
)

/* hipLaunchKernel — EXIT */
LTTNG_UST_TRACEPOINT_EVENT(
    lttng_roctracer,
    hipKernelLaunch_end,
    LTTNG_UST_TP_ARGS(
        uint64_t, correlation_id
    ),
    LTTNG_UST_TP_FIELDS(
        lttng_ust_field_integer(uint64_t, correlation_id, correlation_id)
    )
)

/* hipMemcpy — ENTER: addresses, byte count, direction */
LTTNG_UST_TRACEPOINT_EVENT(
    lttng_roctracer,
    hipMemcpy_begin,
    LTTNG_UST_TP_ARGS(
        uint64_t,     correlation_id,
        const void *, dst,
        const void *, src,
        uint64_t,     bytes,
        int,          kind
    ),
    LTTNG_UST_TP_FIELDS(
        lttng_ust_field_integer(uint64_t,         correlation_id, correlation_id)
        lttng_ust_field_integer_hex(unsigned long, dst, (unsigned long)(dst))
        lttng_ust_field_integer_hex(unsigned long, src, (unsigned long)(src))
        lttng_ust_field_integer(uint64_t,         bytes, bytes)
        lttng_ust_field_integer(int,              kind,  kind)
    )
)

/* hipMemcpy — EXIT */
LTTNG_UST_TRACEPOINT_EVENT(
    lttng_roctracer,
    hipMemcpy_end,
    LTTNG_UST_TP_ARGS(
        uint64_t, correlation_id
    ),
    LTTNG_UST_TP_FIELDS(
        lttng_ust_field_integer(uint64_t, correlation_id, correlation_id)
    )
)

/* hipDeviceSynchronize — ENTER */
LTTNG_UST_TRACEPOINT_EVENT(
    lttng_roctracer,
    hipDeviceSync_begin,
    LTTNG_UST_TP_ARGS(
        uint64_t, correlation_id
    ),
    LTTNG_UST_TP_FIELDS(
        lttng_ust_field_integer(uint64_t, correlation_id, correlation_id)
    )
)

/* hipDeviceSynchronize — EXIT */
LTTNG_UST_TRACEPOINT_EVENT(
    lttng_roctracer,
    hipDeviceSync_end,
    LTTNG_UST_TP_ARGS(
        uint64_t, correlation_id
    ),
    LTTNG_UST_TP_FIELDS(
        lttng_ust_field_integer(uint64_t, correlation_id, correlation_id)
    )
)

/* ================================================================
 * Activity API tracepoints  (GPU-side, from pool flush callback)
 *
 * correlation_id links each activity record to the callback ENTER record.
 * begin_ns / end_ns are GPU-side timestamps (CLOCK_MONOTONIC on MI300A).
 *
 * MI300A note: CPU and GPU share HBM3 — HtoD/DtoH memcpy records will
 * show begin_ns ≈ end_ns (no actual PCIe transfer, intra-HBM copy only).
 * ================================================================ */

/* Kernel execution on GPU */
LTTNG_UST_TRACEPOINT_EVENT(
    lttng_roctracer,
    hipKernelActivity,
    LTTNG_UST_TP_ARGS(
        uint64_t,     correlation_id,
        uint64_t,     begin_ns,
        uint64_t,     end_ns,
        int,          device_id,
        uint32_t,     queue_id,
        const char *, kernel_name
    ),
    LTTNG_UST_TP_FIELDS(
        lttng_ust_field_integer(uint64_t,  correlation_id, correlation_id)
        lttng_ust_field_integer(uint64_t,  begin_ns,       begin_ns)
        lttng_ust_field_integer(uint64_t,  end_ns,         end_ns)
        lttng_ust_field_integer(int,       device_id,      device_id)
        lttng_ust_field_integer(uint32_t,  queue_id,       queue_id)
        lttng_ust_field_string(kernel_name, kernel_name)
    )
)

/* Memcpy execution on GPU */
LTTNG_UST_TRACEPOINT_EVENT(
    lttng_roctracer,
    hipMemcpyActivity,
    LTTNG_UST_TP_ARGS(
        uint64_t, correlation_id,
        uint64_t, begin_ns,
        uint64_t, end_ns,
        int,      device_id,
        uint32_t, queue_id,
        uint64_t, bytes
    ),
    LTTNG_UST_TP_FIELDS(
        lttng_ust_field_integer(uint64_t,  correlation_id, correlation_id)
        lttng_ust_field_integer(uint64_t,  begin_ns,       begin_ns)
        lttng_ust_field_integer(uint64_t,  end_ns,         end_ns)
        lttng_ust_field_integer(int,       device_id,      device_id)
        lttng_ust_field_integer(uint32_t,  queue_id,       queue_id)
        lttng_ust_field_integer(uint64_t,  bytes,          bytes)
    )
)

#endif /* _LTTNG_ROCTRACER_TRACEPOINT_H_ */

#include <lttng/tracepoint-event.h>
