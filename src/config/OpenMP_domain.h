/* OpenMP_domain.h */
#ifndef OPENMP_DOMAIN_H
#define OPENMP_DOMAIN_H

#include "trace_domain_dsl.h"
#include "trace_domain_loader.h"

/* --- 1. DSL BLOCK: OpenMP domain definition (data only) --- */

#define OPENMP_DOMAIN_DEFINITION                          \
    TRACE_DOMAIN_BEGIN("OpenMP")                          \
                                                          \
        /* Punis: [OpenMP.team.thread(0-1023|0-254)]      \
         *        [OpenMP.device(0-16)]                   \
         */                                               \
        TRACE_PUNIT("team",   0,  254)                    \
        TRACE_PUNIT("thread", 0, 1023)                    \
        TRACE_PUNIT("device", 0,   16)                    \
                                                          \
        /* [OpenMP(parallel)] */                          \
        TRACE_SUBDOMAIN_BEGIN("parallel")                 \
            TRACE_EVENT( 0, "omp_parallel_begin", 1)      \
            TRACE_EVENT( 1, "omp_parallel_end",   1)      \
        TRACE_SUBDOMAIN_END()                             \
                                                          \
        /* [OpenMP(thread)] */                            \
        TRACE_SUBDOMAIN_BEGIN("thread")                   \
            TRACE_EVENT( 2, "omp_thread_start",   1)      \
            TRACE_EVENT( 3, "omp_thread_end",     1)      \
        TRACE_SUBDOMAIN_END()                             \
                                                          \
        /* [OpenMP(task)] */                              \
        TRACE_SUBDOMAIN_BEGIN("task")                     \
            TRACE_EVENT( 4, "omp_task_create",    0)      \
            TRACE_EVENT( 5, "omp_task_schedule",  0)      \
            TRACE_EVENT( 6, "omp_task_execute",   0)      \
            TRACE_EVENT( 7, "omp_task_complete",  0)      \
        TRACE_SUBDOMAIN_END()                             \
                                                          \
        /* [OpenMP(workshare)] */                         \
        TRACE_SUBDOMAIN_BEGIN("workshare")                \
            TRACE_EVENT( 8, "omp_loop_begin",     0)      \
            TRACE_EVENT( 9, "omp_loop_end",       0)      \
                                                          \
            TRACE_EVENT(10, "omp_section_begin",  0)      \
            TRACE_EVENT(11, "omp_section_end",    0)      \
                                                          \
            TRACE_EVENT(12, "omp_master_begin",   1)      \
            TRACE_EVENT(13, "omp_master_end",     1)      \
                                                          \
            TRACE_EVENT(14, "omp_single_begin",   0)      \
            TRACE_EVENT(15, "omp_single_end",     0)      \
        TRACE_SUBDOMAIN_END()                             \
                                                          \
        /* [OpenMP(barrier)] */                           \
        TRACE_SUBDOMAIN_BEGIN("barrier")                  \
            TRACE_EVENT(16, "omp_barrier_begin",  0)      \
            TRACE_EVENT(17, "omp_barrier_end",    0)      \
        TRACE_SUBDOMAIN_END()                             \
                                                          \
        /* [OpenMP(critical)] */                          \
        TRACE_SUBDOMAIN_BEGIN("critical")                 \
            TRACE_EVENT(18, "omp_critical_begin", 0)      \
            TRACE_EVENT(19, "omp_critical_end",   0)      \
            TRACE_EVENT(20, "omp_lock_init",      0)      \
            TRACE_EVENT(21, "omp_lock_destroy",   0)      \
            TRACE_EVENT(22, "omp_lock_acquire",   0)      \
            TRACE_EVENT(23, "omp_lock_release",   0)      \
        TRACE_SUBDOMAIN_END()                             \
                                                          \
        /* [OpenMP(atomic)] */                            \
        TRACE_SUBDOMAIN_BEGIN("atomic")                   \
            TRACE_EVENT(24, "omp_atomic_begin",   0)      \
            TRACE_EVENT(25, "omp_atomic_end",     0)      \
        TRACE_SUBDOMAIN_END()                             \
                                                          \
        /* [OpenMP(target)] */                            \
        TRACE_SUBDOMAIN_BEGIN("target")                   \
            TRACE_EVENT(26, "omp_target_begin",        0) \
            TRACE_EVENT(27, "omp_target_end",          0) \
            TRACE_EVENT(28, "omp_target_data_begin",   0) \
            TRACE_EVENT(29, "omp_target_data_end",     0) \
            TRACE_EVENT(30, "omp_target_update_begin", 0) \
            TRACE_EVENT(31, "omp_target_update_end",   0) \
            TRACE_EVENT(32, "omp_taskgroup_begin",     0) \
            TRACE_EVENT(33, "omp_taskgroup_end",       0) \
        TRACE_SUBDOMAIN_END()                             \
                                                          \
        /* [OpenMP(reduction)] */                         \
        TRACE_SUBDOMAIN_BEGIN("reduction")                \
            TRACE_EVENT(34, "omp_reduction_begin", 0)     \
            TRACE_EVENT(35, "omp_reduction_end",   0)     \
        TRACE_SUBDOMAIN_END()                             \
                                                          \
        /* [OpenMP(others)] */                            \
        TRACE_SUBDOMAIN_BEGIN("others")                   \
            TRACE_EVENT(36, "omp_flush",           0)     \
            TRACE_EVENT(37, "omp_cancel",          0)     \
        TRACE_SUBDOMAIN_END()                             \
                                                          \
    TRACE_DOMAIN_END()

/* --- 2. Registration function (inline, but built from the same DSL) --- */
static inline struct domain_info *register_openmp_domain(void)
{
    int domain_index_before = num_domain;

    /* Bind DSL macros to helpers */
    #define TRACE_IMPL_DOMAIN_BEGIN(name)   \
        do {                                \
            dsl_add_domain((name));

    #define TRACE_IMPL_DOMAIN_END()         \
        } while (0)

    #define TRACE_IMPL_PUNIT(name, low, high) \
            dsl_add_punit((name), (low), (high));

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
    return &domain_info_table[domain_index_before];
}

#endif /* OPENMP_DOMAIN_H */
