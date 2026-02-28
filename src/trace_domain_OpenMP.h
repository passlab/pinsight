/* OpenMP_domain.h */
#ifndef OPENMP_DOMAIN_H
#define OPENMP_DOMAIN_H

#include <omp-tools.h>
#include <omp.h>

#include "trace_domain_dsl.h"
#include "trace_domain_loader.h"
#include "trace_config.h"

extern int OpenMP_domain_index;
extern domain_info_t *OpenMP_domain_info;
extern domain_trace_config_t *OpenMP_trace_config;

/* --- 1. DSL BLOCK: OpenMP domain definition (data only) --- */

#define OPENMP_DOMAIN_DEFINITION                          \
    TRACE_DOMAIN_BEGIN("OpenMP", TRACE_EVENT_ID_NATIVE)   \
                                                          \
        /* Punits: [OpenMP.team.thread(0-1023|0-254)]     \
         *        [OpenMP.device(0-16)]                   \
         */                                               \
        TRACE_PUNIT("team",   0,  255, omp_get_team_num)                    \
        TRACE_PUNIT("thread", 0,  255, omp_get_thread_num)                    \
        TRACE_PUNIT("device", 0,   16, omp_get_device_num)                    \
                                                          \
        /* [OpenMP(parallel)] */                          \
        TRACE_SUBDOMAIN_BEGIN("parallel")                 \
            TRACE_EVENT(ompt_callback_parallel_begin, "omp_parallel_begin", 1)      \
            TRACE_EVENT(ompt_callback_parallel_end, "omp_parallel_end",   1)      \
        TRACE_SUBDOMAIN_END()                             \
                                                          \
        /* [OpenMP(thread)] */                            \
        TRACE_SUBDOMAIN_BEGIN("thread")                   \
            TRACE_EVENT(ompt_callback_thread_begin, "omp_thread_begin",   1)      \
            TRACE_EVENT(ompt_callback_thread_end, "omp_thread_end",     1)      \
        TRACE_SUBDOMAIN_END()                             \
                                                          \
        /* [OpenMP(task)] */                              \
        TRACE_SUBDOMAIN_BEGIN("task")                     \
            TRACE_EVENT(ompt_callback_task_create, "omp_task_create",    1)      \
            TRACE_EVENT(ompt_callback_task_schedule, "omp_task_schedule",  1)      \
            TRACE_EVENT(ompt_callback_implicit_task, "omp_implicit_task",   0)      \
            TRACE_EVENT(ompt_callback_dependences, "omp_dependences",   0)      \
            TRACE_EVENT(ompt_callback_task_dependence, "omp_task_dependence",   0)      \
        TRACE_SUBDOMAIN_END()                             \
                                                          \
        /* [OpenMP(workshare)] */                         \
        TRACE_SUBDOMAIN_BEGIN("workshare")                \
            TRACE_EVENT(ompt_callback_work, "omp_work",     1)      \
            TRACE_EVENT(ompt_callback_dispatch, "omp_dispatch",     0)      \
            TRACE_EVENT(ompt_callback_reduction, "omp_reduction",     0)      \
        TRACE_SUBDOMAIN_END()                             \
                                                          \
        /* [OpenMP(sync)] */                           \
        TRACE_SUBDOMAIN_BEGIN("sync")                  \
            TRACE_EVENT(ompt_callback_sync_region_wait, "omp_sync_region_wait",  1)      \
            TRACE_EVENT(ompt_callback_sync_region, "omp_sync_region",    1)      \
            TRACE_EVENT(ompt_callback_masked, "omp_masked",    1)      \
        TRACE_SUBDOMAIN_END()                             \
                                                          \
        /* [OpenMP(critical)] */                          \
        TRACE_SUBDOMAIN_BEGIN("critical")                 \
            TRACE_EVENT(ompt_callback_mutex_acquire, "omp_mutex_acquire", 0)      \
            TRACE_EVENT(ompt_callback_mutex_acquired, "omp_mutex_acquired",   0)      \
            TRACE_EVENT(ompt_callback_mutex_released, "omp_mutex_released",      0)      \
            TRACE_EVENT(ompt_callback_lock_init, "omp_lock_init",   0)      \
            TRACE_EVENT(ompt_callback_lock_destroy, "omp_lock_destroy",   0)      \
            TRACE_EVENT(ompt_callback_nest_lock, "omp_nest_lock",   0)      \
            TRACE_EVENT(ompt_callback_flush, "omp_flush",   0)      \
        TRACE_SUBDOMAIN_END()                             \
                                                          \
        /* [OpenMP(target)] */                            \
        TRACE_SUBDOMAIN_BEGIN("target")                   \
            TRACE_EVENT(ompt_callback_target, "omp_target",        0) \
            TRACE_EVENT(ompt_callback_target_data_op, "omp_target_data_op",          0) \
            TRACE_EVENT(ompt_callback_device_initialize, "omp_device_initialize",   0) \
            TRACE_EVENT(ompt_callback_device_finalize, "omp_device_finalize",     0) \
            TRACE_EVENT(ompt_callback_device_load, "omp_device_load", 0) \
            TRACE_EVENT(ompt_callback_device_unload, "omp_device_unload",   0) \
            TRACE_EVENT(ompt_callback_target_emi, "omp_target_emi",     0) \
            TRACE_EVENT(ompt_callback_target_data_op_emi, "omp_target_data_op_emi",       0) \
            TRACE_EVENT(ompt_callback_target_submit_emi, "omp_target_submit_emi",       0) \
            TRACE_EVENT(ompt_callback_target_map_emi, "omp_target_map_emi",       0) \
        TRACE_SUBDOMAIN_END()                             \
                                                          \
        /* [OpenMP(others)] */                            \
        TRACE_SUBDOMAIN_BEGIN("others")                   \
            TRACE_EVENT(ompt_callback_cancel, "omp_cancel",          0)     \
            TRACE_EVENT(ompt_callback_error, "omp_error",          0)     \
            TRACE_EVENT(ompt_callback_control_tool, "omp_control_tool",          0)     \
        TRACE_SUBDOMAIN_END()                             \
                                                          \
    TRACE_DOMAIN_END()

/* --- 2. Registration function (inline, but built from the same DSL) --- */
static inline struct domain_info *register_OpenMP_trace_domain(void)
{
    int OpenMP_domain_index = num_domain;

    /* Bind DSL macros to helpers */
    #define TRACE_IMPL_DOMAIN_BEGIN(name, mode)   \
        do {                                \
            dsl_add_domain((name), (mode));

    #define TRACE_IMPL_DOMAIN_END()         \
        } while (0)

    #define TRACE_IMPL_PUNIT(name, low, high, punit_id_func, arg, num_arg) \
            dsl_add_punit((name), (low), (high), ((int (*)())(punit_id_func)), (arg), (num_arg));

    #define TRACE_IMPL_SUBDOMAIN_BEGIN(name) \
            { dsl_add_subdomain((name));

    #define TRACE_IMPL_SUBDOMAIN_END() \
            }

    #define TRACE_IMPL_EVENT(native_id, name, initial_status) \
                dsl_add_event((native_id), (name), (initial_status));

    /* Expand DSL block */
    OPENMP_DOMAIN_DEFINITION;

    /* Cleanup macro namespace */
    #undef TRACE_IMPL_EVENT
    #undef TRACE_IMPL_SUBDOMAIN_END
    #undef TRACE_IMPL_SUBDOMAIN_BEGIN
    #undef TRACE_IMPL_PUNIT
    #undef TRACE_IMPL_DOMAIN_END
    #undef TRACE_IMPL_DOMAIN_BEGIN

    /* Return pointer to this domain */
    OpenMP_domain_info = &domain_info_table[OpenMP_domain_index];
    OpenMP_trace_config = &domain_default_trace_config[OpenMP_domain_index];
    return OpenMP_domain_info;
}

#endif /* OPENMP_DOMAIN_H */
