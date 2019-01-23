#undef TRACEPOINT_PROVIDER
#define TRACEPOINT_PROVIDER lttng_pinsight

#undef TRACEPOINT_INCLUDE_FILE
#define TRACEPOINT_INCLUDE_FILE ./lttng_tracepoint.h

#if !defined(_TRACEPOINT_LTTNG_TRACEPOINT_H) || defined(TRACEPOINT_HEADER_MULTI_READ)
#define _TRACEPOINT_LTTNG_TRACEPOINT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <lttng/tracepoint.h>
#include <stdbool.h>

/** Macros used to simplify the definition of LTTng TRACEPOINT_EVENT */
#define COMMON_TP_ARGS
//COMMON_TP_FIELDS are those fields in the thread-local storage. These fields will be added to all the trace records
#define COMMON_TP_FIELDS \
    ctf_integer(unsigned int, global_thread_num, global_thread_num) \
    ctf_integer(unsigned int, omp_thread_num, omp_thread_num) \
    ctf_integer_hex(long int, parallel_codeptr, parallel_codeptr) \
    ctf_integer(unsigned int, parallel_counter, parallel_counter)

//    ctf_integer_hex(long int, task_codeptr, task_codeptr) \
//    ctf_integer(unsigned int, task_counter, task_counter)

#ifdef ENABLE_ENERGY
#define ENERGY_TP_ARGS             \
        ,\
        long long int,    pkg_energy0,\
        long long int,    pkg_energy1,\
        long long int,    pkg_energy2,\
        long long int,    pkg_energy3

#define ENERGY_TP_FIELDS \
        ctf_integer(long long int, pkg_energy0, pkg_energy0) \
        ctf_integer(long long int, pkg_energy1, pkg_energy1) \
        ctf_integer(long long int, pkg_energy2, pkg_energy2) \
        ctf_integer(long long int, pkg_energy3, pkg_energy3)

//package_energy[] is a global variable declared in ompt_callback.h file, since the arguments passed to the
//tracepoint call depend on the ENERGY_TP_ARGS, thus we put here so update will be easy to make them consistent
#define ENERGY_TRACEPOINT_CALL_ARGS ,package_energy[0],package_energy[1],package_energy[2],package_energy[3]
#else
#define ENERGY_TP_ARGS
#define ENERGY_TP_FIELDS
#define ENERGY_TRACEPOINT_CALL_ARGS
#endif

//LTTng TRACEPOINT_EVENT definition
/**
 * thread_begin and thread_end
 */
#define TRACEPOINT_EVENT_THREAD(event_name)                                    \
    TRACEPOINT_EVENT(                                                          \
        lttng_pinsight, event_name,                                            \
        TP_ARGS(                                                               \
            unsigned short,    thread_type                                     \
            ENERGY_TP_ARGS                                                      \
        ),                                                                     \
        TP_FIELDS(                                                             \
            ctf_integer(unsigned int, global_thread_num, global_thread_num) \
            ctf_integer(unsigned short, thread_type, thread_type)      \
            ENERGY_TP_FIELDS                                                \
        )                                                                      \
    )

TRACEPOINT_EVENT_THREAD(thread_begin)
TRACEPOINT_EVENT_THREAD(thread_end)

/**
 * parallel_begin
 */
TRACEPOINT_EVENT(
    lttng_pinsight,
    parallel_begin,
    TP_ARGS(
        unsigned int,         requested_team_size,
        int,                  flag,
        const void *,         parent_task_frame
        ENERGY_TP_ARGS
    ),
    TP_FIELDS(
        COMMON_TP_FIELDS
        ctf_integer(unsigned int, team_size, requested_team_size)
        ctf_integer(int, flag, flag)
        ctf_integer_hex(long int, parent_task_frame, parent_task_frame)
        ENERGY_TP_FIELDS
    )
)

TRACEPOINT_EVENT(
    lttng_pinsight,
    parallel_end,
    TP_ARGS(
        int,           flag
        ENERGY_TP_ARGS
    ),
    TP_FIELDS(
        COMMON_TP_FIELDS
        ctf_integer(int, flag, flag)
        ENERGY_TP_FIELDS
    )
)

/**
 * implicit task begin and end
 */
#define TRACEPOINT_EVENT_IMPLICIT_TASK(event_name)                                    \
    TRACEPOINT_EVENT(                                                          \
        lttng_pinsight, event_name,                                            \
        TP_ARGS(                                                               \
            unsigned int,    team_size                                     \
            ENERGY_TP_ARGS                                                      \
        ),                                                                     \
        TP_FIELDS(                                                             \
            COMMON_TP_FIELDS                                                    \
            ctf_integer(unsigned int, team_size, team_size)      \
            ENERGY_TP_FIELDS                                                \
        )                                                                      \
    )

TRACEPOINT_EVENT_IMPLICIT_TASK(implicit_task_begin)
TRACEPOINT_EVENT_IMPLICIT_TASK(implicit_task_end)

/**
 * thread worksharing work
 */
#define TRACEPOINT_EVENT_WORK(event_name)                                    \
    TRACEPOINT_EVENT(                                                          \
        lttng_pinsight, event_name,                                            \
        TP_ARGS(                                                               \
            unsigned short,    wstype,                                     \
            const void *,  work_begin_codeptr,                                          \
            const void *,  work_end_codeptr,                                          \
            unsigned int,  counter,            \
            unsigned int,  count            \
            ENERGY_TP_ARGS                                                      \
        ),                                                                     \
        TP_FIELDS(                                                             \
            COMMON_TP_FIELDS                                                    \
            ctf_integer(unsigned short, wstype, wstype)      \
            ctf_integer_hex(long int, work_begin_codeptr, work_begin_codeptr)                  \
            ctf_integer_hex(long int, work_end_codeptr, work_end_codeptr)                  \
            ctf_integer(unsigned int, counter, counter)                 \
            ctf_integer(unsigned int, count, count)                        \
            ENERGY_TP_FIELDS                                                \
        )                                                                      \
    )

TRACEPOINT_EVENT_WORK(work_begin)
TRACEPOINT_EVENT_WORK(work_end)

/**
 * master
 */
#define TRACEPOINT_EVENT_MASTER(event_name)                                    \
    TRACEPOINT_EVENT(                                                          \
        lttng_pinsight, event_name,                                            \
        TP_ARGS(                                                               \
            const void *,  master_begin_codeptr,                                          \
            const void *,  master_end_codeptr,                                          \
            unsigned int,  counter            \
            ENERGY_TP_ARGS                                                      \
        ),                                                                     \
        TP_FIELDS(                                                             \
            COMMON_TP_FIELDS                                                    \
            ctf_integer_hex(long int, master_begin_codeptr, master_begin_codeptr)                  \
            ctf_integer_hex(long int, master_end_codeptr, master_end_codeptr)                  \
            ctf_integer(unsigned int, counter, counter)                 \
            ENERGY_TP_FIELDS                                                \
        )                                                                      \
    )

TRACEPOINT_EVENT_MASTER(master_begin)
TRACEPOINT_EVENT_MASTER(master_end)

/* synchronization, e.g. barrier, taskwait, taskgroup, related tracepoint */
#define TRACEPOINT_EVENT_SYNC(event_name)                                    \
    TRACEPOINT_EVENT(                                                          \
        lttng_pinsight, event_name,                                            \
        TP_ARGS(                                                               \
            unsigned short,    kind,                                     \
            const void *,  sync_codeptr,                                          \
            unsigned int,  counter            \
            ENERGY_TP_ARGS                                                      \
        ),                                                                     \
        TP_FIELDS(                                                             \
            COMMON_TP_FIELDS                                                    \
            ctf_integer(unsigned short, kind, kind)      \
            ctf_integer_hex(long int, sync_codeptr, sync_codeptr)                  \
            ctf_integer(unsigned int, counter, counter)                 \
            ENERGY_TP_FIELDS                                                \
        )                                                                      \
    )

TRACEPOINT_EVENT_SYNC(sync_begin)
TRACEPOINT_EVENT_SYNC(sync_end)
TRACEPOINT_EVENT_SYNC(sync_wait_begin)
TRACEPOINT_EVENT_SYNC(sync_wait_end)

#ifdef __cplusplus
}
#endif

#endif /* _TRACEPOINT_LTTNG_TRACEPOINT_H */

/* This part must be outside ifdef protection */
#include <lttng/tracepoint-event.h>
