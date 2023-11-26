#undef LTTNG_UST_TRACEPOINT_PROVIDER
#define LTTNG_UST_TRACEPOINT_PROVIDER ompt_pinsight_lttng_ust

#undef LTTNG_UST_TRACEPOINT_INCLUDE
#define LTTNG_UST_TRACEPOINT_INCLUDE "./ompt_lttng_ust_tracepoint.h"

#if !defined(_OMPT_LTTNG_UST_TRACEPOINT_H_) || defined(LTTNG_UST_TRACEPOINT_HEADER_MULTI_READ)
#define _OMPT_LTTNG_UST_TRACEPOINT_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <lttng/tracepoint.h>
#include <stdbool.h>

#ifndef _OMPT_LTTNG_UST_TRACEPOINT_H_DECLARE_ONCE_
#define _OMPT_LTTNG_UST_TRACEPOINT_H_DECLARE_ONCE_
#ifdef PINSIGHT_MPI
extern int mpirank ;
#include <common_tp_fields_global_lttng_ust_tracepoint.h>
#endif
#ifdef PINSIGHT_BACKTRACE
#include <backtrace.h>
#else
#define LTTNG_UST_TP_FIELDS_BACKTRACE
#endif
extern __thread int global_thread_num;
extern __thread int omp_thread_num;
extern __thread const void * parallel_codeptr;
extern __thread unsigned int parallel_record_id;
extern __thread const void * task_codeptr;
extern __thread unsigned int task_record_id;
#endif

/** Macros used to simplify the definition of LTTNG_UST_TRACEPOINT_EVENT */
//COMMON_LTTNG_UST_TP_FIELDS_OMPT are those fields in the thread-local storage. These fields will be added to all the trace records
#if defined(PINSIGHT_MPI)
#define COMMON_LTTNG_UST_TP_FIELDS_OMPT \
    COMMON_LTTNG_UST_TP_FIELDS_GLOBAL \
	LTTNG_UST_TP_FIELDS_BACKTRACE \
    lttng_ust_field_integer(unsigned int, mpirank, mpirank) \
    lttng_ust_field_integer(unsigned int, global_thread_num, global_thread_num) \
    lttng_ust_field_integer(unsigned int, omp_thread_num, omp_thread_num) \
    lttng_ust_field_integer_hex(unsigned long int, parallel_codeptr, parallel_codeptr) \
    lttng_ust_field_integer(unsigned int, parallel_record_id, parallel_record_id)
#else
#define COMMON_LTTNG_UST_TP_FIELDS_OMPT \
	LTTNG_UST_TP_FIELDS_BACKTRACE \
    lttng_ust_field_integer(unsigned int, global_thread_num, global_thread_num) \
    lttng_ust_field_integer(unsigned int, omp_thread_num, omp_thread_num) \
    lttng_ust_field_integer_hex(unsigned long int, parallel_codeptr, parallel_codeptr) \
    lttng_ust_field_integer(unsigned int, parallel_record_id, parallel_record_id)
#endif
// For future tasking extension of OpenMP
//    lttng_ust_field_integer_hex(long int, task_codeptr, task_codeptr) \
//    lttng_ust_field_integer(unsigned int, task_record_id, task_record_id)

#ifdef PINSIGHT_ENERGY
#define ENERGY_LTTNG_UST_TP_ARGS             \
        ,\
        long long int,    pkg_energy0,\
        long long int,    pkg_energy1,\
        long long int,    pkg_energy2,\
        long long int,    pkg_energy3

#define ENERGY_LTTNG_UST_TP_FIELDS \
        lttng_ust_field_integer(long long int, pkg_energy0, pkg_energy0) \
        lttng_ust_field_integer(long long int, pkg_energy1, pkg_energy1) \
        lttng_ust_field_integer(long long int, pkg_energy2, pkg_energy2) \
        lttng_ust_field_integer(long long int, pkg_energy3, pkg_energy3)

//package_energy[] is a global variable declared in ompt_callback.h file, since the arguments passed to the
//tracepoint call depend on the ENERGY_LTTNG_UST_TP_ARGS, thus we put here so update will be easy to make them consistent
#define ENERGY_LTTNG_UST_TRACEPOINT_CALL_ARGS ,package_energy[0],package_energy[1],package_energy[2],package_energy[3]
#else
#define ENERGY_LTTNG_UST_TP_ARGS
#define ENERGY_LTTNG_UST_TP_FIELDS
#define ENERGY_LTTNG_UST_TRACEPOINT_CALL_ARGS
#endif

//LTTNG_UST_TRACEPOINT_EVENT definition
/**
 * thread_begin and thread_end
 */
#define LTTNG_UST_TRACEPOINT_EVENT_OMPT_THREAD(event_name)                                    \
    LTTNG_UST_TRACEPOINT_EVENT(                                                          \
        ompt_pinsight_lttng_ust, event_name,                                            \
        LTTNG_UST_TP_ARGS(                                                               \
            unsigned short,    thread_type                                     \
            ENERGY_LTTNG_UST_TP_ARGS                                                      \
        ),                                                                     \
        LTTNG_UST_TP_FIELDS(                                                             \
            COMMON_LTTNG_UST_TP_FIELDS_OMPT                                               \
            ENERGY_LTTNG_UST_TP_FIELDS                                                \
        )                                                                      \
    )

//lttng_ust_field_integer(unsigned short, thread_type, thread_type)      \

LTTNG_UST_TRACEPOINT_EVENT_OMPT_THREAD(thread_begin)
LTTNG_UST_TRACEPOINT_EVENT_OMPT_THREAD(thread_end)

/**
 * parallel_begin
 */
LTTNG_UST_TRACEPOINT_EVENT(
    ompt_pinsight_lttng_ust,
    parallel_begin,
    LTTNG_UST_TP_ARGS(
        unsigned int,         requested_team_size,
        int,                  flag,
        const void *,         parent_task_frame
        ENERGY_LTTNG_UST_TP_ARGS
    ),
    LTTNG_UST_TP_FIELDS(
        COMMON_LTTNG_UST_TP_FIELDS_OMPT
        lttng_ust_field_integer(unsigned int, team_size, requested_team_size)
        lttng_ust_field_integer(int, flag, flag)
        lttng_ust_field_integer_hex(long int, parent_task_frame, parent_task_frame)
        ENERGY_LTTNG_UST_TP_FIELDS
    )
)

LTTNG_UST_TRACEPOINT_EVENT(
    ompt_pinsight_lttng_ust,
    parallel_end,
    LTTNG_UST_TP_ARGS(
        int,           flag
        ENERGY_LTTNG_UST_TP_ARGS
    ),
    LTTNG_UST_TP_FIELDS(
        COMMON_LTTNG_UST_TP_FIELDS_OMPT
        lttng_ust_field_integer(int, flag, flag)
        ENERGY_LTTNG_UST_TP_FIELDS
    )
)

/**
 * implicit task begin and end
 */
#define LTTNG_UST_TRACEPOINT_EVENT_OMPT_IMPLICIT_TASK(event_name)                                    \
    LTTNG_UST_TRACEPOINT_EVENT(                                                          \
        ompt_pinsight_lttng_ust, event_name,                                            \
        LTTNG_UST_TP_ARGS(                                                               \
            unsigned int,    team_size                                     \
            ENERGY_LTTNG_UST_TP_ARGS                                                      \
        ),                                                                     \
        LTTNG_UST_TP_FIELDS(                                                             \
            COMMON_LTTNG_UST_TP_FIELDS_OMPT                                                    \
            lttng_ust_field_integer(unsigned int, team_size, team_size)      \
            ENERGY_LTTNG_UST_TP_FIELDS                                                \
        )                                                                      \
    )

LTTNG_UST_TRACEPOINT_EVENT_OMPT_IMPLICIT_TASK(implicit_task_begin)
LTTNG_UST_TRACEPOINT_EVENT_OMPT_IMPLICIT_TASK(implicit_task_end)

/**
 * thread worksharing work
 */
#define LTTNG_UST_TRACEPOINT_EVENT_OMPT_WORK(event_name)                                    \
    LTTNG_UST_TRACEPOINT_EVENT(                                                          \
        ompt_pinsight_lttng_ust, event_name,                                            \
        LTTNG_UST_TP_ARGS(                                                               \
            unsigned short,    wstype,                                     \
            const void *,  work_begin_codeptr,                                          \
            const void *,  work_end_codeptr,                                          \
            unsigned int,  record_id,            \
            unsigned int,  count            \
            ENERGY_LTTNG_UST_TP_ARGS                                                      \
        ),                                                                     \
        LTTNG_UST_TP_FIELDS(                                                             \
            COMMON_LTTNG_UST_TP_FIELDS_OMPT                                                    \
            lttng_ust_field_integer(unsigned short, wstype, wstype)      \
            lttng_ust_field_integer_hex(unsigned long int, work_begin_codeptr, work_begin_codeptr)                  \
            lttng_ust_field_integer_hex(unsigned long int, work_end_codeptr, work_end_codeptr)                  \
            lttng_ust_field_integer(unsigned int, record_id, record_id)                 \
            lttng_ust_field_integer(unsigned int, count, count)                        \
            ENERGY_LTTNG_UST_TP_FIELDS                                                \
        )                                                                      \
    )

LTTNG_UST_TRACEPOINT_EVENT_OMPT_WORK(work_begin)
LTTNG_UST_TRACEPOINT_EVENT_OMPT_WORK(work_end)

/**
 * masked
 */
#define LTTNG_UST_TRACEPOINT_EVENT_OMPT_MASKED(event_name)                                    \
    LTTNG_UST_TRACEPOINT_EVENT(                                               \
        ompt_pinsight_lttng_ust, event_name,                                            \
        LTTNG_UST_TP_ARGS(                                                               \
            const void *,  masked_begin_codeptr,                                          \
            const void *,  masked_end_codeptr,                                          \
            unsigned int,  record_id            \
            ENERGY_LTTNG_UST_TP_ARGS                                                      \
        ),                                                                     \
        LTTNG_UST_TP_FIELDS(                                                             \
            COMMON_LTTNG_UST_TP_FIELDS_OMPT                                                    \
            lttng_ust_field_integer_hex(unsigned long int, masked_begin_codeptr, masked_begin_codeptr)                  \
            lttng_ust_field_integer_hex(unsigned long int, masked_end_codeptr, masked_end_codeptr)                  \
            lttng_ust_field_integer(unsigned int, record_id, record_id)                 \
            ENERGY_LTTNG_UST_TP_FIELDS                                                \
        )                                                                      \
    )

LTTNG_UST_TRACEPOINT_EVENT_OMPT_MASKED(masked_begin)
LTTNG_UST_TRACEPOINT_EVENT_OMPT_MASKED(masked_end)

/* synchronization: explicit barrier */
#define LTTNG_UST_TRACEPOINT_EVENT_OMPT_BARRIER_EXPLICIT_SYNC(event_name)                                    \
    LTTNG_UST_TRACEPOINT_EVENT(                                                          \
        ompt_pinsight_lttng_ust, event_name,                                            \
        LTTNG_UST_TP_ARGS(                                                               \
            unsigned short,    kind,                                     \
            const void *,  sync_codeptr,                                          \
            unsigned int,  record_id            \
            ENERGY_LTTNG_UST_TP_ARGS                                                      \
        ),                                                                     \
        LTTNG_UST_TP_FIELDS(                                                             \
            COMMON_LTTNG_UST_TP_FIELDS_OMPT                                                    \
            lttng_ust_field_integer(unsigned short, kind, kind)      \
            lttng_ust_field_integer_hex(unsigned long int, sync_codeptr, sync_codeptr)                  \
            lttng_ust_field_integer(unsigned int, record_id, record_id)                 \
            ENERGY_LTTNG_UST_TP_FIELDS                                                \
        )                                                                      \
    )

LTTNG_UST_TRACEPOINT_EVENT_OMPT_BARRIER_EXPLICIT_SYNC(barrier_explicit_sync_begin)
LTTNG_UST_TRACEPOINT_EVENT_OMPT_BARRIER_EXPLICIT_SYNC(barrier_explicit_sync_end)
LTTNG_UST_TRACEPOINT_EVENT_OMPT_BARRIER_EXPLICIT_SYNC(barrier_explicit_sync_wait_begin)
LTTNG_UST_TRACEPOINT_EVENT_OMPT_BARRIER_EXPLICIT_SYNC(barrier_explicit_sync_wait_end)

/* synchronization, e.g. implicit barrier for parallel/teams/for, etc. not sure taskwait and taskgroup yet */
#define LTTNG_UST_TRACEPOINT_EVENT_OMPT_BARRIER_IMPLICIT_SYNC(event_name)                                    \
    LTTNG_UST_TRACEPOINT_EVENT(                                                          \
        ompt_pinsight_lttng_ust, event_name,                                            \
        LTTNG_UST_TP_ARGS(                                                               \
            unsigned short,    kind,                                     \
            const void *,  sync_codeptr                                          \
            ENERGY_LTTNG_UST_TP_ARGS                                                      \
        ),                                                                     \
        LTTNG_UST_TP_FIELDS(                                                             \
            COMMON_LTTNG_UST_TP_FIELDS_OMPT                                                    \
            lttng_ust_field_integer(unsigned short, kind, kind)      \
            lttng_ust_field_integer_hex(unsigned long int, sync_codeptr, sync_codeptr)                  \
            ENERGY_LTTNG_UST_TP_FIELDS                                                \
        )                                                                      \
    )

LTTNG_UST_TRACEPOINT_EVENT_OMPT_BARRIER_IMPLICIT_SYNC(barrier_implicit_sync_begin)
LTTNG_UST_TRACEPOINT_EVENT_OMPT_BARRIER_IMPLICIT_SYNC(barrier_implicit_sync_end)
LTTNG_UST_TRACEPOINT_EVENT_OMPT_BARRIER_IMPLICIT_SYNC(barrier_implicit_sync_wait_begin)
LTTNG_UST_TRACEPOINT_EVENT_OMPT_BARRIER_IMPLICIT_SYNC(barrier_implicit_sync_wait_end)


/**
 * parallel join begin and end
 * There are OMPT event for sync_begin, sync_end, sync_wait_begin and sync_wait_end,
 * We are able to detect when each thread enters
 * into join barrier (before sync_region) and when it completes the joining (upon the parallel_end)
 */
#define LTTNG_UST_TRACEPOINT_EVENT_OMPT_PARALLEL_JOIN(event_name)                                    \
    LTTNG_UST_TRACEPOINT_EVENT(                                                          \
        ompt_pinsight_lttng_ust, event_name,                                            \
        LTTNG_UST_TP_ARGS(                                                               \
            unsigned short,    useless                                     \
            ENERGY_LTTNG_UST_TP_ARGS                                                      \
        ),                                                                     \
        LTTNG_UST_TP_FIELDS(                                                             \
            COMMON_LTTNG_UST_TP_FIELDS_OMPT                                                    \
            lttng_ust_field_integer(unsigned short, useless, useless)      \
            ENERGY_LTTNG_UST_TP_FIELDS                                                \
        )                                                                      \
    )

LTTNG_UST_TRACEPOINT_EVENT_OMPT_PARALLEL_JOIN(parallel_join_sync_begin)
LTTNG_UST_TRACEPOINT_EVENT_OMPT_PARALLEL_JOIN(parallel_join_sync_end)
LTTNG_UST_TRACEPOINT_EVENT_OMPT_PARALLEL_JOIN(parallel_join_sync_wait_begin)
LTTNG_UST_TRACEPOINT_EVENT_OMPT_PARALLEL_JOIN(parallel_join_sync_wait_end)

#ifdef __cplusplus
}
#endif

#endif /* _OMPT_LTTNG_UST_TRACEPOINT_H_ */

/* This part must be outside ifdef protection */
#include <lttng/tracepoint-event.h>
