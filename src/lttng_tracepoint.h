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
#include <ompt.h>
#include <stdbool.h>

TRACEPOINT_EVENT(
    lttng_pinsight,
    parallel_begin,
    TP_ARGS(
        int,                  gtid,
        const omp_frame_t *,  parent_task_frame,
        ompt_data_t *,        parallel_data,
        uint32_t,             requested_team_size,
        const void *,         codeptr_ra
#ifdef ENABLE_ENERGY
        ,
        long long int,        pkg_energy0,
        long long int,        pkg_energy1,
        long long int,        pkg_energy2,
        long long int,        pkg_energy3
#endif
    ),
    TP_FIELDS(
        ctf_integer(int, gtid, gtid)
        //                ctf_integer_hex(long int, frame,
        //parent_task_frame->exit_frame)
        ctf_integer_hex(long int, frame, parent_task_frame->enter_frame)
        ctf_integer_hex(long int, parallel_id, parallel_data->value)
        ctf_integer(int, team_size, requested_team_size)
        ctf_integer_hex(long int, codeptr_ra, codeptr_ra)
#ifdef ENABLE_ENERGY
        ctf_integer(long long int, pkg_energy0, pkg_energy0)
        ctf_integer(long long int, pkg_energy1, pkg_energy1)
        ctf_integer(long long int, pkg_energy2, pkg_energy2)
        ctf_integer(long long int, pkg_energy3, pkg_energy3)
#endif
    )
)

TRACEPOINT_EVENT(
    lttng_pinsight,
    parallel_end,
    TP_ARGS(
        int,           gtid,
        ompt_data_t *, parallel_data,
        ompt_data_t *, task_data,
        const void *,  codeptr_ra
#ifdef ENABLE_ENERGY
        ,
        long long int, pkg_energy0,
        long long int, pkg_energy1,
        long long int, pkg_energy2,
        long long int, pkg_energy3
#endif
    ),
    TP_FIELDS(
        ctf_integer(int, gtid, gtid)
        ctf_integer_hex(long int, parallel_id, parallel_data->value)
        ctf_integer_hex(long int, task_id, task_data->value)
        ctf_integer_hex(long int, codeptr_ra, codeptr_ra)
#ifdef ENABLE_ENERGY
        ctf_integer(long long int, pkg_energy0, pkg_energy0)
        ctf_integer(long long int, pkg_energy1, pkg_energy1)
        ctf_integer(long long int, pkg_energy2, pkg_energy2)
        ctf_integer(long long int, pkg_energy3, pkg_energy3)
#endif
    )
)

/**
 * thread work
 */
#if !defined(ENABLE_ENERGY)
#define TRACEPOINT_EVENT_WORK(event_name)                                      \
    TRACEPOINT_EVENT(                                                          \
        lttng_pinsight, event_name,                                            \
        TP_ARGS(                                                               \
            int,               gtid,                                           \
            ompt_data_t *,     parallel_data,                                  \
            ompt_data_t *,     task_data,                                      \
            unsigned long int, count,                                          \
            const void *,      codeptr_ra                                      \
        ),                                                                     \
        TP_FIELDS(                                                             \
            ctf_integer(int, gtid, gtid)                                       \
            ctf_integer_hex(long int, parallel_id, (parallel_data) ? parallel_data->value : 0) \
            ctf_integer_hex(long int, task_id, task_data->value)               \
            ctf_integer(unsigned long int, count, count)                       \
            ctf_integer_hex(long int, codeptr_ra, codeptr_ra)                  \
        )                                                                      \
    )
#else
#define TRACEPOINT_EVENT_WORK(event_name)                                      \
    TRACEPOINT_EVENT(                                                          \
        lttng_pinsight, event_name,                                            \
        TP_ARGS(                                                               \
            int,               gtid,                                           \
            ompt_data_t *,     parallel_data,                                  \
            ompt_data_t *,     task_data,                                      \
            unsigned long int, count,                                          \
            const void *,      codeptr_ra,                                     \
            long long int,     pkg_energy0,                                    \
            long long int,     pkg_energy1,                                    \
            long long int,     pkg_energy2,                                    \
            long long int,     pkg_energy3                                     \
        ),                                                                     \
        TP_FIELDS(                                                             \
            ctf_integer(int, gtid, gtid)                                       \
            ctf_integer_hex(long int, parallel_id, (parallel_data) ? parallel_data->value : 0) \
            ctf_integer_hex(long int, task_id, task_data->value)               \
            ctf_integer(unsigned long int, count, count)                       \
            ctf_integer_hex(long int, codeptr_ra, codeptr_ra)                  \
            ctf_integer(long long int, pkg_energy0, pkg_energy0)               \
            ctf_integer(long long int, pkg_energy1, pkg_energy1)               \
            ctf_integer(long long int, pkg_energy2, pkg_energy2)               \
            ctf_integer(long long int, pkg_energy3, pkg_energy3)               \
        )                                                                      \
    )
#endif

TRACEPOINT_EVENT_WORK(work_loop_begin)
TRACEPOINT_EVENT_WORK(work_loop_end)
TRACEPOINT_EVENT_WORK(work_sections_begin)
TRACEPOINT_EVENT_WORK(work_sections_end)
TRACEPOINT_EVENT_WORK(work_single_executor_begin)
TRACEPOINT_EVENT_WORK(work_single_executor_end)
TRACEPOINT_EVENT_WORK(work_single_other_begin)
TRACEPOINT_EVENT_WORK(work_single_other_end)
TRACEPOINT_EVENT_WORK(work_workshare_begin)
TRACEPOINT_EVENT_WORK(work_workshare_end)
TRACEPOINT_EVENT_WORK(work_distribute_begin)
TRACEPOINT_EVENT_WORK(work_distribute_end)
TRACEPOINT_EVENT_WORK(work_taskloop_begin)
TRACEPOINT_EVENT_WORK(work_taskloop_end)

/**
 * master
 */
#if !defined(ENABLE_ENERGY)
#define TRACEPOINT_EVENT_MASTER(event_name)                                    \
    TRACEPOINT_EVENT(                                                          \
        lttng_pinsight, event_name,                                            \
        TP_ARGS(                                                               \
            int,           gtid,                                               \
            ompt_data_t *, parallel_data,                                      \
            ompt_data_t *, task_data,                                          \
            const void *,  codeptr_ra                                          \
        ),                                                                     \
        TP_FIELDS(                                                             \
            ctf_integer(int, gtid, gtid)                                       \
            ctf_integer_hex(long int, parallel_id, (parallel_data) ? parallel_data->value : 0) \
            ctf_integer_hex(long int, task_id, task_data->value)               \
            ctf_integer_hex(long int, codeptr_ra, codeptr_ra)                  \
        )                                                                      \
    )
#else
#define TRACEPOINT_EVENT_MASTER(event_name)                                    \
    TRACEPOINT_EVENT(                                                          \
        lttng_pinsight, event_name,                                            \
        TP_ARGS(                                                               \
            int,           gtid,                                               \
            ompt_data_t *, parallel_data,                                      \
            ompt_data_t *, task_data,                                          \
            const void *,  codeptr_ra,                                         \
            long long int, pkg_energy0,                                        \
            long long int, pkg_energy1,                                        \
            long long int, pkg_energy2,                                        \
            long long int, pkg_energy3                                         \
        ),                                                                     \
        TP_FIELDS(                                                             \
            ctf_integer(int, gtid, gtid)                                       \
            ctf_integer_hex(long int, parallel_id, (parallel_data) ? parallel_data->value : 0) \
            ctf_integer_hex(long int, task_id, task_data->value)               \
            ctf_integer_hex(long int, codeptr_ra, codeptr_ra)                  \
            ctf_integer(long long int, pkg_energy0, pkg_energy0)               \
            ctf_integer(long long int, pkg_energy1, pkg_energy1)               \
            ctf_integer(long long int, pkg_energy2, pkg_energy2)               \
            ctf_integer(long long int, pkg_energy3, pkg_energy3)               \
        )                                                                      \
    )
#endif

TRACEPOINT_EVENT_MASTER(master_begin)
TRACEPOINT_EVENT_MASTER(master_end)

/**
 * implicit task begin and end
 */
#if !defined(ENABLE_ENERGY)
#define TRACEPOINT_EVENT_IMPLICIT_TASK(event_name)                             \
    TRACEPOINT_EVENT(                                                          \
        lttng_pinsight, event_name,                                            \
        TP_ARGS(                                                               \
            int,           gtid,                                               \
            ompt_data_t *, parallel_data,                                      \
            ompt_data_t *, task_data,                                          \
            unsigned int,  team_size,                                          \
            unsigned int,  thread_num                                          \
        ),                                                                     \
        TP_FIELDS(                                                             \
            ctf_integer(int, gtid, gtid)                                       \
            ctf_integer_hex(long int, parallel_id, (parallel_data) ? parallel_data->value : 0) \
            ctf_integer_hex(long int, task_id, task_data->value)               \
            ctf_integer(int, team_size, team_size)                             \
            ctf_integer_hex(int, thread_num, thread_num)                       \
        )                                                                      \
    )
#else
#define TRACEPOINT_EVENT_IMPLICIT_TASK(event_name)                             \
    TRACEPOINT_EVENT(                                                          \
        lttng_pinsight, event_name,                                            \
        TP_ARGS(                                                               \
            int,           gtid,                                               \
            ompt_data_t *, parallel_data,                                      \
            ompt_data_t *, task_data,                                          \
            unsigned int,  team_size,                                          \
            unsigned int,  thread_num,                                         \
            long long int, pkg_energy0,                                        \
            long long int, pkg_energy1,                                        \
            long long int, pkg_energy2,                                        \
            long long int, pkg_energy3                                         \
        ),                                                                     \
        TP_FIELDS(                                                             \
            ctf_integer(int, gtid, gtid)                                       \
            ctf_integer_hex(long int, parallel_id, (parallel_data) ? parallel_data->value : 0) \
            ctf_integer_hex(long int, task_id, task_data->value)               \
            ctf_integer(int, team_size, team_size)                             \
            ctf_integer_hex(int, thread_num, thread_num)                       \
            ctf_integer(long long int, pkg_energy0, pkg_energy0)               \
            ctf_integer(long long int, pkg_energy1, pkg_energy1)               \
            ctf_integer(long long int, pkg_energy2, pkg_energy2)               \
            ctf_integer(long long int, pkg_energy3, pkg_energy3)               \
        )                                                                      \
    )
#endif

TRACEPOINT_EVENT_IMPLICIT_TASK(implicit_task_begin)
TRACEPOINT_EVENT_IMPLICIT_TASK(implicit_task_end)

/* synchronization, e.g. barrier, taskwait, taskgroup, related tracepoint */
#if !defined(ENABLE_ENERGY)
#define TRACEPOINT_EVENT_SYNC(event_name)                                      \
    TRACEPOINT_EVENT(                                                          \
        lttng_pinsight, event_name,                                            \
        TP_ARGS(                                                               \
            int,           gtid,                                               \
            ompt_data_t *, parallel_data,                                      \
            ompt_data_t *, task_data,                                          \
            const void *,  codeptr_ra                                          \
        ),                                                                     \
        TP_FIELDS(                                                             \
            ctf_integer(int, gtid, gtid)                                       \
            ctf_integer_hex(long int, parallel_id, (parallel_data) ? parallel_data->value : 0) \
            ctf_integer_hex(long int, task_id, task_data->value)               \
            ctf_integer_hex(long int, codeptr_ra, codeptr_ra)                  \
        )                                                                      \
    )
#else
#define TRACEPOINT_EVENT_SYNC(event_name)                                      \
    TRACEPOINT_EVENT(                                                          \
        lttng_pinsight, event_name,                                            \
        TP_ARGS(                                                               \
            int,           gtid,                                               \
            ompt_data_t *, parallel_data,                                      \
            ompt_data_t *, task_data,                                          \
            const void *,  codeptr_ra,                                         \
            long long int, pkg_energy0,                                        \
            long long int, pkg_energy1,                                        \
            long long int, pkg_energy2,                                        \
            long long int, pkg_energy3                                         \
        ),                                                                     \
        TP_FIELDS(                                                             \
            ctf_integer(int, gtid, gtid)                                       \
            ctf_integer_hex(long int, parallel_id, (parallel_data) ? parallel_data->value : 0) \
            ctf_integer_hex(long int, task_id, task_data->value)               \
            ctf_integer_hex(long int, codeptr_ra, codeptr_ra)                  \
            ctf_integer(long long int, pkg_energy0, pkg_energy0)               \
            ctf_integer(long long int, pkg_energy1, pkg_energy1)               \
            ctf_integer(long long int, pkg_energy2, pkg_energy2)               \
            ctf_integer(long long int, pkg_energy3, pkg_energy3)               \
        )                                                                      \
    )
#endif

TRACEPOINT_EVENT_SYNC(barrier_begin)
TRACEPOINT_EVENT_SYNC(barrier_end)
TRACEPOINT_EVENT_SYNC(taskwait_begin)
TRACEPOINT_EVENT_SYNC(taskwait_end)
TRACEPOINT_EVENT_SYNC(taskgroup_begin)
TRACEPOINT_EVENT_SYNC(taskgroup_end)
TRACEPOINT_EVENT_SYNC(barrier_wait_begin)
TRACEPOINT_EVENT_SYNC(barrier_wait_end)
TRACEPOINT_EVENT_SYNC(taskwait_wait_begin)
TRACEPOINT_EVENT_SYNC(taskwait_wait_end)
TRACEPOINT_EVENT_SYNC(taskgroup_wait_begin)
TRACEPOINT_EVENT_SYNC(taskgroup_wait_end)

/**
 * thread-related events
 */
#if !defined(ENABLE_ENERGY)
#define TRACEPOINT_EVENT_THREAD(event_name)                                    \
    TRACEPOINT_EVENT(                                                          \
        lttng_pinsight, event_name,                                            \
        TP_ARGS(                                                               \
            int, gtid,                                                         \
            ompt_data_t *, thread_data                                         \
        ),                                                                     \
        TP_FIELDS(                                                             \
            ctf_integer(int, gtid, gtid)                                       \
            ctf_integer_hex(long int, thread_data_id, thread_data->value)      \
        )                                                                      \
    )
#else
#define TRACEPOINT_EVENT_THREAD(event_name)                                    \
    TRACEPOINT_EVENT(                                                          \
        lttng_pinsight, event_name,                                            \
        TP_ARGS(                                                               \
            int, gtid,                                                         \
            ompt_data_t *, thread_data,                                        \
            long long int, pkg_energy0,                                        \
            long long int, pkg_energy1,                                        \
            long long int, pkg_energy2,                                        \
            long long int, pkg_energy3                                         \
        ),                                                                     \
        TP_FIELDS(                                                             \
            ctf_integer(int, gtid, gtid)                                       \
            ctf_integer_hex(long int, thread_data_id, thread_data->value)      \
            ctf_integer(long long int, pkg_energy0, pkg_energy0)               \
            ctf_integer(long long int, pkg_energy1, pkg_energy1)               \
            ctf_integer(long long int, pkg_energy2, pkg_energy2)               \
            ctf_integer(long long int, pkg_energy3, pkg_energy3)               \
        )                                                                      \
    )
#endif

TRACEPOINT_EVENT_THREAD(thread_begin)
TRACEPOINT_EVENT_THREAD(thread_end)

#ifdef __cplusplus
}
#endif

#endif /* _TRACEPOINT_LTTNG_TRACEPOINT_H */

/* This part must be outside ifdef protection */
#include <lttng/tracepoint-event.h>
