//
// PInsight energy/power — dedicated LTTng-UST tracepoint provider.
//
// Decoupled from the OpenMP/MPI/CUDA/HIP/Python providers so it can be enabled
// or disabled independently in an LTTng session
// (e.g. `lttng enable-event -u 'energy_pinsight_lttng_ust:*'`).
//
// All energy events share one field layout: the common global fields plus a
// fixed 4 CPU sockets + 4 GPU devices (unmeasured slots carry 0) and a `seq`
// field (0 for enter/exit; a monotonic counter for the future energy_sample).
//
#undef LTTNG_UST_TRACEPOINT_PROVIDER
#define LTTNG_UST_TRACEPOINT_PROVIDER energy_pinsight_lttng_ust

#undef LTTNG_UST_TRACEPOINT_INCLUDE
#define LTTNG_UST_TRACEPOINT_INCLUDE "./energy_lttng_ust_tracepoint.h"

#if !defined(_ENERGY_LTTNG_UST_TRACEPOINT_H_) || defined(LTTNG_UST_TRACEPOINT_HEADER_MULTI_READ)
#define _ENERGY_LTTNG_UST_TRACEPOINT_H_

#include <lttng/tracepoint.h>
#include <stdint.h>

#ifndef _ENERGY_LTTNG_UST_TRACEPOINT_H_ONCE_
#define _ENERGY_LTTNG_UST_TRACEPOINT_H_ONCE_
#include "common_tp_fields_global_lttng_ust_tracepoint.h"
#endif

/* Shared argument and field lists for every energy event. */
#define ENERGY_LTTNG_UST_TP_ARGS \
        uint64_t, cpu0_uj, uint64_t, cpu1_uj, uint64_t, cpu2_uj, uint64_t, cpu3_uj, \
        uint64_t, gpu0_mj, uint64_t, gpu1_mj, uint64_t, gpu2_mj, uint64_t, gpu3_mj, \
        uint64_t, seq

#define ENERGY_LTTNG_UST_TP_FIELDS \
        COMMON_LTTNG_UST_TP_FIELDS_GLOBAL \
        lttng_ust_field_integer(uint64_t, cpu0_uj, cpu0_uj) \
        lttng_ust_field_integer(uint64_t, cpu1_uj, cpu1_uj) \
        lttng_ust_field_integer(uint64_t, cpu2_uj, cpu2_uj) \
        lttng_ust_field_integer(uint64_t, cpu3_uj, cpu3_uj) \
        lttng_ust_field_integer(uint64_t, gpu0_mj, gpu0_mj) \
        lttng_ust_field_integer(uint64_t, gpu1_mj, gpu1_mj) \
        lttng_ust_field_integer(uint64_t, gpu2_mj, gpu2_mj) \
        lttng_ust_field_integer(uint64_t, gpu3_mj, gpu3_mj) \
        lttng_ust_field_integer(uint64_t, seq, seq)

#define LTTNG_UST_TRACEPOINT_EVENT_ENERGY(event_name)        \
    LTTNG_UST_TRACEPOINT_EVENT(                               \
        energy_pinsight_lttng_ust, event_name,               \
        LTTNG_UST_TP_ARGS(ENERGY_LTTNG_UST_TP_ARGS),         \
        LTTNG_UST_TP_FIELDS(ENERGY_LTTNG_UST_TP_FIELDS)      \
    )

LTTNG_UST_TRACEPOINT_EVENT_ENERGY(energy_enter)
LTTNG_UST_TRACEPOINT_EVENT_ENERGY(energy_exit)

#endif /* _ENERGY_LTTNG_UST_TRACEPOINT_H_ */

#include <lttng/tracepoint-event.h>
