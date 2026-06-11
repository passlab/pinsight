//
// PInsight energy/power — dedicated LTTng-UST tracepoint provider.
//
// Decoupled from the OpenMP/MPI/CUDA/HIP/Python providers so it can be enabled
// or disabled independently in an LTTng session
// (e.g. `lttng enable-event -u 'energy_pinsight_lttng_ust:*'`).
//
// All energy events share one field layout: the common global fields plus two
// dynamic-length sequences in microjoules — one per-CPU-socket, one per-GPU/
// accelerator-device (incl. the MI300A combined APU package) — and a `seq` field
// (0 for enter/exit; a monotonic counter for the future energy_sample). Using
// LTTng sequences instead of fixed cpuN/gpuN scalars removes any ceiling on
// socket/device count: the sequence length is the number of discovered sockets/
// devices, the element position is the source index, and an element is 0 when
// that index was not measured.
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

/* Shared argument and field lists for every energy event.
 * cpu_uj / gpu_mj are pointers to the per-index arrays; num_cpu / num_gpu are
 * their lengths (number of discovered sockets / devices). */
#define ENERGY_LTTNG_UST_TP_ARGS \
        unsigned int, num_cpu, uint64_t *, cpu_uj, \
        unsigned int, num_gpu, uint64_t *, gpu_uj, \
        uint64_t, seq

#define ENERGY_LTTNG_UST_TP_FIELDS \
        COMMON_LTTNG_UST_TP_FIELDS_GLOBAL \
        lttng_ust_field_sequence(uint64_t, cpu_uj, cpu_uj, unsigned int, num_cpu) \
        lttng_ust_field_sequence(uint64_t, gpu_uj, gpu_uj, unsigned int, num_gpu) \
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
