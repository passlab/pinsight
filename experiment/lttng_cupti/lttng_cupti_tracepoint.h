/**
 *
 * Copyright (c) Yonghong Yan (yanyh15@ github or gamil) and
 * PASSLab (http://passlab.github.io/). All rights reserved.
 *
 * This file is subject to the terms and conditions defined in
 * file 'LICENSE.txt', which is part of this source code package.
 *
 * lttng_cupti_tracepoint.h
 * LTTng UST tracepoint definition for CUPTI callback API, so far, only cudaMemcpy, kernel launch and synchronization are provided
 *
 */
#undef LTTNG_UST_TRACEPOINT_PROVIDER
#define LTTNG_UST_TRACEPOINT_PROVIDER lttng_pinsight_cuda

#undef LTTNG_UST_TRACEPOINT_INCLUDE
#define LTTNG_UST_TRACEPOINT_INCLUDE ./lttng_cupti_tracepoint.h

#if !defined(LTTNG_CUPTI_TRACEPOINT_H) || defined(LTTNG_UST_TRACEPOINT_HEADER_MULTI_READ)
#define LTTNG_CUPTI_TRACEPOINT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <lttng/tracepoint.h>
#include <stdbool.h>

#ifndef LTTNG_CUPTI_TRACEPOINT_H_DECLARE_ONCE
#define LTTNG_CUPTI_TRACEPOINT_H_DECLARE_ONCE
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
#define COMMON_LTTNG_UST_TP_FIELDS_MPI_OMP \
    ctf_integer(unsigned int, mpirank, mpirank) \
    ctf_integer(unsigned int, global_thread_num, global_thread_num) \
    ctf_integer(unsigned int, omp_thread_num, omp_thread_num) \
    ctf_integer(unsigned int, cuda_codeptr, codeptr) \
    ctf_string(kernel_func, func_name)

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
TRACEPOINT_ENUM(lttng_pinsight_cuda, cudaMemcpyKind_enum,
        LTTNG_UST_TP_ENUM_VALUES(
            ctf_enum_value("cudaMemcpyHostToHost", 0)
            ctf_enum_value("cudaMemcpyHostToDevice", 1)
            ctf_enum_value("cudaMemcpyDeviceToHost", 2)
            ctf_enum_value("cudaMemcpyDeviceToDevice", 3)
            ctf_enum_value("cudaMemcpyDefault", 3)
        )
)

/* for cudaMemcpy event */
LTTNG_UST_TRACEPOINT_EVENT(
        lttng_pinsight_cuda,
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
            ctf_integer(unsigned int, dst, dst)
            ctf_integer(unsigned int, src, src)
            ctf_integer(unsigned int, count, count)
            ctf_enum(lttng_pinsight_cuda, cudaMemcpyKind_enum, int, enumfield, kind)
        )
)

LTTNG_UST_TRACEPOINT_EVENT(
        lttng_pinsight_cuda,
        cudaMemcpy_end,
        LTTNG_UST_TP_ARGS(
            CODEPTR_ARG,
            int, return_val
        ),
        LTTNG_UST_TP_FIELDS(
            COMMON_LTTNG_UST_TP_FIELDS_MPI_OMP
            ctf_integer(int, return_val, return_val)
        )
)

/* for kernel launch event */
LTTNG_UST_TRACEPOINT_EVENT(
        lttng_pinsight_cuda,
        cudaKernelLaunch_begin,
        LTTNG_UST_TP_ARGS(
            CODEPTR_ARG
        ),
        LTTNG_UST_TP_FIELDS(
            COMMON_LTTNG_UST_TP_FIELDS_MPI_OMP
        )
)

LTTNG_UST_TRACEPOINT_EVENT(
        lttng_pinsight_cuda,
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

#endif /* LTTNG_CUPTI_TRACEPOINT_H */

/* This part must be outside ifdef protection */
#include <lttng/tracepoint-event.h>
