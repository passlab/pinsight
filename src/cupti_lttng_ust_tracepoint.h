/**
 *
 * Copyright (c) Yonghong Yan (yanyh15@ github or gmail) and
 * PASSLab (http://passlab.github.io/). All rights reserved.
 *
 * This file is subject to the terms and conditions defined in
 * file 'LICENSE.txt', which is part of this source code package.
 *
 * cupti_lttng_tracepoint.h
 * LTTng UST tracepoint definition for CUPTI callback API, so far, only cudaMemcpy,
 * kernel launch and synchronization are provided
 *
 */
#undef LTTNG_UST_TRACEPOINT_PROVIDER
#define LTTNG_UST_TRACEPOINT_PROVIDER cupti_pinsight_lttng_ust

#undef LTTNG_UST_TRACEPOINT_INCLUDE
#define LTTNG_UST_TRACEPOINT_INCLUDE "./cupti_lttng_ust_tracepoint.h"

#if !defined(_CUPTI_LTTNG_UST_TRACEPOINT_H_) || defined(LTTNG_UST_TRACEPOINT_HEADER_MULTI_READ)
#define _CUPTI_LTTNG_UST_TRACEPOINT_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <lttng/tracepoint.h>
#include <stdbool.h>

#ifndef _CUPTI_LTTNG_UST_TRACEPOINT_H_DECLARE_ONCE_
#define _CUPTI_LTTNG_UST_TRACEPOINT_H_DECLARE_ONCE_
#ifdef PINSIGHT_MPI
extern int mpirank ;
#endif
#ifdef PINSIGHT_OPENMP
extern __thread int global_thread_num;
extern __thread int omp_thread_num;
#endif
#endif

/** Macros used to simplify the definition of LTTng LTTNG_UST_TRACEPOINT_EVENT */
#define CODEPTR_ARG \
    const void *, codeptr, \
    const char *, func_name

//COMMON_LTTNG_UST_TP_FIELDS_PMPI are those fields in the thread-local storage. These fields will be added to all the trace records
#if defined(PINSIGHT_MPI) && defined(PINSIGHT_OPENMP)
#define COMMON_LTTNG_UST_TP_FIELDS_MPI_OMP \
    lttng_ust_field_integer(unsigned int, mpirank, mpirank) \
    lttng_ust_field_integer(unsigned int, global_thread_num, global_thread_num) \
    lttng_ust_field_integer(unsigned int, omp_thread_num, omp_thread_num) \
    lttng_ust_field_integer(unsigned int, cuda_codeptr, codeptr) \
    lttng_ust_field_string(kernel_func, func_name)
#elif defined(PINSIGHT_MPI) && !defined(PINSIGHT_OPENMP)
#define COMMON_LTTNG_UST_TP_FIELDS_MPI_OMP \
    lttng_ust_field_integer(unsigned int, mpirank, mpirank) \
    lttng_ust_field_integer(unsigned int, cuda_codeptr, codeptr) \
    lttng_ust_field_string(kernel_func, func_name)
#elif !defined(PINSIGHT_MPI) && defined(PINSIGHT_OPENMP)
#define COMMON_LTTNG_UST_TP_FIELDS_MPI_OMP \
    lttng_ust_field_integer(unsigned int, global_thread_num, global_thread_num) \
    lttng_ust_field_integer(unsigned int, omp_thread_num, omp_thread_num) \
    lttng_ust_field_integer(unsigned int, cuda_codeptr, codeptr) \
    lttng_ust_field_string(kernel_func, func_name)
#else
#define COMMON_LTTNG_UST_TP_FIELDS_MPI_OMP \
    lttng_ust_field_integer(unsigned int, cuda_codeptr, codeptr) \
    lttng_ust_field_string(kernel_func, func_name)
#endif


/* define cudaMemcpyKind enum
        cudaMemcpyHostToHost = 0
Host -> Host
        cudaMemcpyHostToDevice = 1
Host -> Device
        cudaMemcpyDeviceToHost = 2
Device -> Host
        cudaMemcpyDeviceToDevice = 3
Device -> Device
        cudaMemcpyDefault = 4
*/
LTTNG_UST_TRACEPOINT_ENUM(cupti_pinsight_lttng_ust, cudaMemcpyKind_enum,
        LTTNG_UST_TP_ENUM_VALUES(
            lttng_ust_field_enum_value("cudaMemcpyHostToHost", 0)
            lttng_ust_field_enum_value("cudaMemcpyHostToDevice", 1)
            lttng_ust_field_enum_value("cudaMemcpyDeviceToHost", 2)
            lttng_ust_field_enum_value("cudaMemcpyDeviceToDevice", 3)
            lttng_ust_field_enum_value("cudaMemcpyDefault", 3)
        )
)

/* for cudaMemcpy event */
LTTNG_UST_TRACEPOINT_EVENT(
        cupti_pinsight_lttng_ust,
        cudaMemcpy_begin,
        LTTNG_UST_TP_ARGS(
            CODEPTR_ARG,
            void *, dst,
            const void *, src,
            size_t, count,
            int, kind
        ),
        LTTNG_UST_TP_FIELDS(
            COMMON_LTTNG_UST_TP_FIELDS_MPI_OMP
            lttng_ust_field_integer(unsigned int, dst, dst)
            lttng_ust_field_integer(unsigned int, src, src)
            lttng_ust_field_integer(unsigned int, count, count)
            lttng_ust_field_enum(cupti_pinsight_lttng_ust, cudaMemcpyKind_enum, int, enumfield, kind)
        )
)

LTTNG_UST_TRACEPOINT_EVENT(
        cupti_pinsight_lttng_ust,
        cudaMemcpy_end,
        LTTNG_UST_TP_ARGS(
            CODEPTR_ARG,
            int, return_val
        ),
        LTTNG_UST_TP_FIELDS(
            COMMON_LTTNG_UST_TP_FIELDS_MPI_OMP
            lttng_ust_field_integer(int, return_val, return_val)
        )
)

/* for kernel launch event */
LTTNG_UST_TRACEPOINT_EVENT(
        cupti_pinsight_lttng_ust,
        cudaKernelLaunch_begin,
        LTTNG_UST_TP_ARGS(
            CODEPTR_ARG
        ),
        LTTNG_UST_TP_FIELDS(
            COMMON_LTTNG_UST_TP_FIELDS_MPI_OMP
        )
)

LTTNG_UST_TRACEPOINT_EVENT(
        cupti_pinsight_lttng_ust,
        cudaKernelLaunch_end,
        LTTNG_UST_TP_ARGS(
            CODEPTR_ARG
        ),
        LTTNG_UST_TP_FIELDS(
            COMMON_LTTNG_UST_TP_FIELDS_MPI_OMP
        )
)

#ifdef __cplusplus
}
#endif

#endif /* _CUPTI_LTTNG_UST_TRACEPOINT_H_ */

/* This part must be outside ifdef protection */
#include <lttng/tracepoint-event.h>
