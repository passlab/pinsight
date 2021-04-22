//
// Created by Yonghong Yan on 3/3/20.
//

#undef TRACEPOINT_PROVIDER
#define TRACEPOINT_PROVIDER lttng_pinsight_enter_exit

#undef TRACEPOINT_INCLUDE
#define TRACEPOINT_INCLUDE "./enter_exit_pinsight_tracepoint.h"

#if !defined(PINSIGHT_ENTER_EXIT_PINSIGHT_TRACEPOINT_H) || defined(TRACEPOINT_HEADER_MULTI_READ)
#define PINSIGHT_ENTER_EXIT_PINSIGHT_TRACEPOINT_H

#include <lttng/tracepoint.h>

#ifndef PINSIGHT_ENTER_EXIT_PINSIGHT_TRACEPOINT_H_ONCE
#define PINSIGHT_ENTER_EXIT_PINSIGHT_TRACEPOINT_H_ONCE
#include "common_tp_fields_global_lttng_tracepoint.h"
#if defined(PINSIGHT_MPI)
extern int mpirank;
#define COMMON_TP_FIELDS_MPI \
    ctf_integer(unsigned int, mpirank, mpirank)
#else
#define COMMON_TP_FIELDS_MPI
#endif

#if defined(PINSIGHT_OPENMP)
extern __thread int global_thread_num;
extern __thread int omp_thread_num;
#define COMMON_TP_FIELDS_OMP \
    ctf_integer(unsigned int, global_thread_num, global_thread_num) \
    ctf_integer(unsigned int, omp_thread_num, omp_thread_num)
#else
#define COMMON_TP_FIELDS_OMP
#endif

#endif

TRACEPOINT_EVENT(
        lttng_pinsight_enter_exit,
        enter_pinsight,
        TP_ARGS(
        ),
        TP_FIELDS(
            COMMON_TP_FIELDS_GLOBAL
            COMMON_TP_FIELDS_MPI
            COMMON_TP_FIELDS_OMP
        )
)

TRACEPOINT_EVENT(
        lttng_pinsight_enter_exit,
        exit_pinsight,
        TP_ARGS(
        ),
        TP_FIELDS(
            COMMON_TP_FIELDS_GLOBAL
            COMMON_TP_FIELDS_MPI
            COMMON_TP_FIELDS_OMP
        )
)

#endif /* PINSIGHT_ENTER_EXIT_PINSIGHT_TRACEPOINT_H */

#include <lttng/tracepoint-event.h>