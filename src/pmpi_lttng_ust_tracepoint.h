/**
 *
 * Copyright (c) Yonghong Yan (yanyh15@ github or gamil) and PASSLab (http://passlab.github.io/). All rights reserved.
 *
 * This file is subject to the terms and conditions defined in
 * file 'LICENSE.txt', which is part of this source code package.
 *
 * LTTng UST tracepoint definition for MPI, so far, only MPI_Init/Finalize/Send/Recv/Reduce/Allreduce/Barrier
 *
 */
#undef LTTNG_UST_TRACEPOINT_PROVIDER
#define LTTNG_UST_TRACEPOINT_PROVIDER pmpi_pinsight_lttng_ust

#undef LTTNG_UST_TRACEPOINT_INCLUDE
#define LTTNG_UST_TRACEPOINT_INCLUDE "./pmpi_lttng_ust_tracepoint.h"

#if !defined(_PMPI_LTTNG_UST_TRACEPOINT_H_) || defined(LTTNG_UST_TRACEPOINT_HEADER_MULTI_READ)
#define _PMPI_LTTNG_UST_TRACEPOINT_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <lttng/tracepoint.h>
#include <stdbool.h>

/* Because of the way this file is being used multiple times in preprocessing by LTTNG,
 * we need to protect this declaration to appear only once by using the #ifndef way
 *
 * This myrank global variable will be recorded in every PMPI event, thus we define it
 * as global variable and we do not need to pass it as argument.
 */
#ifndef _PMPI_LTTNG_UST_TRACEPOINT_H_DECLARE_ONCE_
#define _PMPI_LTTNG_UST_TRACEPOINT_H_DECLARE_ONCE_
extern int mpirank;
extern __thread void * mpi_codeptr;
#ifdef PINSIGHT_OPENMP
extern __thread int global_thread_num;
extern __thread int omp_thread_num;
#endif
#include <common_tp_fields_global_lttng_ust_tracepoint.h>
#endif

//COMMON_LTTNG_UST_TP_FIELDS_PMPI includes mpirank global variable, and codeptr passed by all the tracepoint call.
// These fields will be added to all the trace records
#ifdef PINSIGHT_OPENMP
#define COMMON_LTTNG_UST_TP_FIELDS_PMPI \
    COMMON_LTTNG_UST_TP_FIELDS_GLOBAL \
    lttng_ust_field_integer(unsigned int, mpirank, mpirank) \
    lttng_ust_field_integer(unsigned int, global_thread_num, global_thread_num) \
    lttng_ust_field_integer(unsigned int, omp_thread_num, omp_thread_num) \
    lttng_ust_field_integer_hex(unsigned int, mpi_codeptr, mpi_codeptr)
#else
#define COMMON_LTTNG_UST_TP_FIELDS_PMPI \
    COMMON_LTTNG_UST_TP_FIELDS_GLOBAL \
    lttng_ust_field_integer(unsigned int, mpirank, mpirank) \
    lttng_ust_field_integer_hex(unsigned int, mpi_codeptr, mpi_codeptr)
#endif

/* int MPI_Init(int *argc, char ***argv) */
LTTNG_UST_TRACEPOINT_EVENT(
        pmpi_pinsight_lttng_ust,
        MPI_Init_begin,
        LTTNG_UST_TP_ARGS(
            unsigned int,        useless
        ),
        LTTNG_UST_TP_FIELDS(
            COMMON_LTTNG_UST_TP_FIELDS_PMPI
        )
)

LTTNG_UST_TRACEPOINT_EVENT(
        pmpi_pinsight_lttng_ust,
        MPI_Init_end,
        LTTNG_UST_TP_ARGS(
            int, return_value
        ),
        LTTNG_UST_TP_FIELDS(
            COMMON_LTTNG_UST_TP_FIELDS_PMPI
            lttng_ust_field_integer(int, return_value, return_value)
        )
)

/* int MPI_Init_thread(int *argc, char ***argv, int required, int *provided) */
LTTNG_UST_TRACEPOINT_EVENT(
        pmpi_pinsight_lttng_ust,
        MPI_Init_thread_begin,
        LTTNG_UST_TP_ARGS(
            int, required
        ),
        LTTNG_UST_TP_FIELDS(
            COMMON_LTTNG_UST_TP_FIELDS_PMPI
            lttng_ust_field_integer(int, required, required)
        )
)

LTTNG_UST_TRACEPOINT_EVENT(
        pmpi_pinsight_lttng_ust,
        MPI_Init_thread_end,
        LTTNG_UST_TP_ARGS(
            int, provided,
            int, return_value
        ),
        LTTNG_UST_TP_FIELDS(
            COMMON_LTTNG_UST_TP_FIELDS_PMPI
            lttng_ust_field_integer(int, provided, provided)
            lttng_ust_field_integer(int, return_value, return_value)
        )
)

/**
 * For the tracepoints immediately after most PMPI_* call, we only need to record necessary info such as mpirank, codeptr,
 * and the return value of the PMPI call. Since those records have exactly same fields, we can use LTTNG_UST_TRACEPOINT_EVENT_CLASS
 * and LTTNG_UST_TRACEPOINT_EVENT_INSTANCE to define. LTTNG_UST_TRACEPOINT_EVENT_CLASS is used to define a class of those events that have
 * the same record fields, and it unfortunatelly requires their LTTNG_UST_TP_ARGS (tracepoint call arguments) are the same (limitation
 * of C preprocessing macros). Then LTTNG_UST_TRACEPOINT_EVENT_INSTANCE can be used to define the LTTNG_UST_TRACEPOINT_EVENT for a specific
 * event. When defining via LTTNG_UST_TRACEPOINT_EVENT_INSTANCE, LTTNG_UST_TP_FIELDS must be specified and to be exactly the same as
 * LTTNG_UST_TRACEPOINT_EVENT_CLASS, and LTTNG_UST_TP_FIELDS should be omitted.
 */
LTTNG_UST_TRACEPOINT_EVENT_CLASS(
        pmpi_pinsight_lttng_ust,
        MPI_Func_end_class,
        LTTNG_UST_TP_ARGS(
            int, return_value
        ),

        LTTNG_UST_TP_FIELDS(
            COMMON_LTTNG_UST_TP_FIELDS_PMPI
            lttng_ust_field_integer(int, return_value, return_value)
        )
)

/**int MPI_Finalize(void) */
LTTNG_UST_TRACEPOINT_EVENT(
        pmpi_pinsight_lttng_ust,
        MPI_Finalize_begin,
        LTTNG_UST_TP_ARGS(
            int, useless_but_needed
        ),
        LTTNG_UST_TP_FIELDS(
            COMMON_LTTNG_UST_TP_FIELDS_PMPI
        )
)

LTTNG_UST_TRACEPOINT_EVENT_INSTANCE(
        pmpi_pinsight_lttng_ust,
        MPI_Func_end_class,
        pmpi_pinsight_lttng_ust,
        MPI_Finalize_end,
        LTTNG_UST_TP_ARGS(
            int, return_value
        )
)

/* int MPI_Send(const void *buf, int count, MPI_Datatype datatype, int dest, int tag, MPI_Comm comm) */
LTTNG_UST_TRACEPOINT_EVENT(
        pmpi_pinsight_lttng_ust, MPI_Send_begin,
        LTTNG_UST_TP_ARGS(
            const void *,  buf,
            unsigned int,  count,
            unsigned int,  dest,
            unsigned int,  tag
        ),
        LTTNG_UST_TP_FIELDS(
            COMMON_LTTNG_UST_TP_FIELDS_PMPI
            lttng_ust_field_integer_hex(unsigned int, buf, buf)
            lttng_ust_field_integer(unsigned int, count, count)
            lttng_ust_field_integer(unsigned int, dest, dest)
            lttng_ust_field_integer(unsigned int, tag, tag)
        )
)

/**
 * use MPI_Func_end_class LTTNG_UST_TRACEPOINT_EVENT_CLASS definition created before
 */
LTTNG_UST_TRACEPOINT_EVENT_INSTANCE(
        pmpi_pinsight_lttng_ust,
        MPI_Func_end_class,
        pmpi_pinsight_lttng_ust,
        MPI_Send_end,
        LTTNG_UST_TP_ARGS(
            int, return_value
        )
)

/* int MPI_Recv(void *buf, int count, MPI_Datatype datatype, int source, int tag, MPI_Comm comm, MPI_Status * status)

typedef struct _MPI_Status {
  int count;
  int cancelled;
  int MPI_SOURCE;
  int MPI_TAG;
  int MPI_ERROR;
} MPI_Status, *PMPI_Status;

 */
LTTNG_UST_TRACEPOINT_EVENT(
        pmpi_pinsight_lttng_ust, MPI_Recv_begin,
        LTTNG_UST_TP_ARGS(
            const void *,  buf,
            unsigned int,  count,
            unsigned int,  source,
            unsigned int,  tag
        ),
        LTTNG_UST_TP_FIELDS(
            COMMON_LTTNG_UST_TP_FIELDS_PMPI
            lttng_ust_field_integer_hex(unsigned int, buf, buf)
            lttng_ust_field_integer(unsigned int, count, count)
            lttng_ust_field_integer(unsigned int, source, source)
            lttng_ust_field_integer(unsigned int, tag, tag)
        )
)

/** TODO: ideally, one may want the end tracepoint to record the real transfer status based on the status of the call */
LTTNG_UST_TRACEPOINT_EVENT_INSTANCE(
        pmpi_pinsight_lttng_ust,
        MPI_Func_end_class,
        pmpi_pinsight_lttng_ust,
        MPI_Recv_end,
        LTTNG_UST_TP_ARGS(
            int, return_value
        )
)

/* int MPI_Barrier( MPI_Comm comm ) */
LTTNG_UST_TRACEPOINT_EVENT(
        pmpi_pinsight_lttng_ust, MPI_Barrier_begin,
        LTTNG_UST_TP_ARGS(
            int, useless_but_needed
        ),
        LTTNG_UST_TP_FIELDS(
            COMMON_LTTNG_UST_TP_FIELDS_PMPI
        )
)

LTTNG_UST_TRACEPOINT_EVENT_INSTANCE(
        pmpi_pinsight_lttng_ust,
        MPI_Func_end_class,
        pmpi_pinsight_lttng_ust,
        MPI_Barrier_end,
        LTTNG_UST_TP_ARGS(
            int, return_value
        )
)

/* int MPI_Reduce(const void *sendbuf, void *recvbuf, int count, MPI_Datatype datatype,
               MPI_Op op, int root, MPI_Comm comm) */
LTTNG_UST_TRACEPOINT_EVENT(
        pmpi_pinsight_lttng_ust, MPI_Reduce_begin,
        LTTNG_UST_TP_ARGS(
            const void *,  sendbuf,
            const void *,  recvbuf,
            unsigned int,  count,
            unsigned int,  root,
            void *,  mpi_op
        ),
        LTTNG_UST_TP_FIELDS(
            COMMON_LTTNG_UST_TP_FIELDS_PMPI
            lttng_ust_field_integer_hex(unsigned int, sendbuf, sendbuf)
            lttng_ust_field_integer_hex(unsigned int, recvbuf, recvbuf)
            lttng_ust_field_integer(unsigned int, count, count)
            lttng_ust_field_integer(unsigned int, root, root)
            lttng_ust_field_integer_hex(unsigned int, mpi_op, mpi_op)
        )
)

LTTNG_UST_TRACEPOINT_EVENT_INSTANCE(
        pmpi_pinsight_lttng_ust,
        MPI_Func_end_class,
        pmpi_pinsight_lttng_ust,
        MPI_Reduce_end,
        LTTNG_UST_TP_ARGS(
            int, return_value
        )
)

/* int MPI_Allreduce(const void *sendbuf, void *recvbuf, int count,
                  MPI_Datatype datatype, MPI_Op op, MPI_Comm comm)*/
LTTNG_UST_TRACEPOINT_EVENT(
        pmpi_pinsight_lttng_ust, MPI_Allreduce_begin,
        LTTNG_UST_TP_ARGS(
            const void *,  sendbuf,
            const void *,  recvbuf,
            unsigned int,  count,
            void *,  mpi_op
        ),
        LTTNG_UST_TP_FIELDS(
            COMMON_LTTNG_UST_TP_FIELDS_PMPI
            lttng_ust_field_integer_hex(unsigned int, sendbuf, sendbuf)
            lttng_ust_field_integer_hex(unsigned int, recvbuf, recvbuf)
            lttng_ust_field_integer(unsigned int, count, count)
            lttng_ust_field_integer_hex(unsigned int, mpi_op, mpi_op)
        )
)

LTTNG_UST_TRACEPOINT_EVENT_INSTANCE(
        pmpi_pinsight_lttng_ust,
        MPI_Func_end_class,
        pmpi_pinsight_lttng_ust,
        MPI_Allreduce_end,
        LTTNG_UST_TP_ARGS(
            int, return_value
        )
)

#ifdef __cplusplus
}
#endif

#endif /* _PMPI_LTTNG_UST_TRACEPOINT_H_ */

/* This part must be outside ifdef protection */
#include <lttng/tracepoint-event.h>
