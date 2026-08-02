//
// PInsight manifest — dedicated LTTng-UST tracepoint provider (WS1).
//
// Decoupled from the domain providers so a session can enable it
// independently: `lttng enable-event -u 'pinsight_manifest_lttng_ust:*'`.
//
// A manifest BURST = one manifest_process followed by N manifest_kv events
// sharing the same `seq` (monotonic per process). Bursts are re-emitted
// periodically and at lifecycle/window transitions; consumers treat them as
// idempotent, LATEST-WINS PER KEY per (hostname, pid) — never rely on stream
// identity or event adjacency (shared per-UID buffers). A snapshot may slice
// a burst; per-key latest-wins makes that harmless. Full design:
// doc/design/ws1_manifest_design.md.
//
#undef LTTNG_UST_TRACEPOINT_PROVIDER
#define LTTNG_UST_TRACEPOINT_PROVIDER pinsight_manifest_lttng_ust

#undef LTTNG_UST_TRACEPOINT_INCLUDE
#define LTTNG_UST_TRACEPOINT_INCLUDE "./manifest_lttng_ust_tracepoint.h"

#if !defined(_MANIFEST_LTTNG_UST_TRACEPOINT_H_) || defined(LTTNG_UST_TRACEPOINT_HEADER_MULTI_READ)
#define _MANIFEST_LTTNG_UST_TRACEPOINT_H_

#include <lttng/tracepoint.h>
#include <stdint.h>

#ifndef _MANIFEST_LTTNG_UST_TRACEPOINT_H_ONCE_
#define _MANIFEST_LTTNG_UST_TRACEPOINT_H_ONCE_
#include "common_tp_fields_global_lttng_ust_tracepoint.h"
#endif

/* Burst header: fixed high-frequency facts. reason = init | mpi_init |
 * periodic | window | fini. mpirank is best-effort from launcher env until
 * MPI_Init makes it authoritative (-1 unknown). window_gen = the cyclic
 * TRACING-window generation counter (producer-visible window identity,
 * design §2.6). nprocs_hint = launcher-env job size (-1 unknown). */
LTTNG_UST_TRACEPOINT_EVENT(
    pinsight_manifest_lttng_ust,
    manifest_process,
    LTTNG_UST_TP_ARGS(
        uint64_t, seq,
        const char *, reason,
        int, mpirank_v,
        const char *, exe,
        uint32_t, window_gen,
        int, nprocs_hint
    ),
    LTTNG_UST_TP_FIELDS(
        COMMON_LTTNG_UST_TP_FIELDS_GLOBAL
        lttng_ust_field_integer(uint64_t, seq, seq)
        lttng_ust_field_string(reason, reason)
        lttng_ust_field_integer(int, mpirank, mpirank_v)
        lttng_ust_field_string(exe, exe)
        lttng_ust_field_integer(uint32_t, window_gen, window_gen)
        lttng_ust_field_integer(int, nprocs_hint, nprocs_hint)
    )
)

/* Generic fact: one (key, value) per event — adding a new fact needs no
 * schema/XML/reader change. Standard key dictionary in design §2.2
 * (pinsight.* / launcher.* / gpu.* / cpu.* / host.* / run_id; app.* is
 * reserved for the future app-note API, user.* for user-provided facts).
 * Values are capped at 256 bytes ("..." marks truncation). */
LTTNG_UST_TRACEPOINT_EVENT(
    pinsight_manifest_lttng_ust,
    manifest_kv,
    LTTNG_UST_TP_ARGS(
        uint64_t, seq,
        const char *, key,
        const char *, value
    ),
    LTTNG_UST_TP_FIELDS(
        COMMON_LTTNG_UST_TP_FIELDS_GLOBAL
        lttng_ust_field_integer(uint64_t, seq, seq)
        lttng_ust_field_string(key, key)
        lttng_ust_field_string(value, value)
    )
)

#endif /* _MANIFEST_LTTNG_UST_TRACEPOINT_H_ */

#include <lttng/tracepoint-event.h>
