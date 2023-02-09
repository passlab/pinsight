//
// Created by Yonghong Yan on 3/3/20.
//

#undef LTTNG_UST_TRACEPOINT_PROVIDER
#define LTTNG_UST_TRACEPOINT_PROVIDER pinsight_enter_exit_lttng_ust

#undef LTTNG_UST_TRACEPOINT_INCLUDE
#define LTTNG_UST_TRACEPOINT_INCLUDE "./enter_exit_lttng_ust_tracepoint.h"

#if !defined(_ENTER_EXIT_LTTNG_UST_TRACEPOINT_H_) || defined(LTTNG_UST_TRACEPOINT_HEADER_MULTI_READ)
#define _ENTER_EXIT_LTTNG_UST_TRACEPOINT_H_

#include <lttng/tracepoint.h>

#ifndef _ENTER_EXIT_LTTNG_UST_TRACEPOINT_H_ONCE_
#define _ENTER_EXIT_LTTNG_UST_TRACEPOINT_H_ONCE_
#include "common_tp_fields_global_lttng_ust_tracepoint.h"
#if defined(PINSIGHT_MPI)
extern int mpirank;
#define COMMON_LTTNG_UST_TP_FIELDS_MPI \
    lttng_ust_field_integer(unsigned int, mpirank, mpirank)
#else
#define COMMON_LTTNG_UST_TP_FIELDS_MPI
#endif

#if defined(PINSIGHT_OPENMP)
extern __thread int global_thread_num;
extern __thread int omp_thread_num;
#define COMMON_LTTNG_UST_TP_FIELDS_OMP \
    lttng_ust_field_integer(unsigned int, global_thread_num, global_thread_num) \
    lttng_ust_field_integer(unsigned int, omp_thread_num, omp_thread_num)
#else
#define COMMON_LTTNG_UST_TP_FIELDS_OMP
#endif

#endif

LTTNG_UST_TRACEPOINT_EVENT(
		pinsight_enter_exit_lttng_ust,
        enter_pinsight,
        LTTNG_UST_TP_ARGS(
        ),
        LTTNG_UST_TP_FIELDS(
            COMMON_LTTNG_UST_TP_FIELDS_GLOBAL
            COMMON_LTTNG_UST_TP_FIELDS_MPI
            COMMON_LTTNG_UST_TP_FIELDS_OMP
        )
)

LTTNG_UST_TRACEPOINT_EVENT(
		pinsight_enter_exit_lttng_ust,
        exit_pinsight,
        LTTNG_UST_TP_ARGS(
        ),
        LTTNG_UST_TP_FIELDS(
            COMMON_LTTNG_UST_TP_FIELDS_GLOBAL
            COMMON_LTTNG_UST_TP_FIELDS_MPI
            COMMON_LTTNG_UST_TP_FIELDS_OMP
        )
)

#endif /* _ENTER_EXIT_LTTNG_UST_TRACEPOINT_H_ */

#include <lttng/tracepoint-event.h>
