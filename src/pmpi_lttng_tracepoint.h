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
#undef TRACEPOINT_PROVIDER
#define TRACEPOINT_PROVIDER lttng_pinsight_pmpi

#undef TRACEPOINT_INCLUDE_FILE
#define TRACEPOINT_INCLUDE_FILE pmpi_lttng_tracepoint.h

#if !defined(_TRACEPOINT_PMPI_LTTNG_TRACEPOINT_H) || defined(TRACEPOINT_HEADER_MULTI_READ)
#define _TRACEPOINT_PMPI_LTTNG_TRACEPOINT_H

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
#ifndef _TRACEPOINT_PMPI_LTTNG_TRACEPOINT_H_DECLARE_ONCE
#define _TRACEPOINT_PMPI_LTTNG_TRACEPOINT_H_DECLARE_ONCE
extern int mpirank ;
#endif

// This MUST be the first argument of any tracepoint definition because the way
// the PMPI_CALL_PROLOGUE and PMPI_CALL_EPILOGUE macros are defined, check pmpi_mpi.c file.
#define CODEPTR_ARG \
    const void *, codeptr

//COMMON_TP_FIELDS_PMPI includes mpirank global variable, and codeptr passed by all the tracepoint call.
// These fields will be added to all the trace records
#define COMMON_TP_FIELDS_PMPI \
    ctf_integer(unsigned int, mpirank, mpirank) \
    ctf_integer_hex(unsigned int, mpi_codeptr, codeptr)

/* int MPI_Init(int *argc, char ***argv) */
TRACEPOINT_EVENT(
        lttng_pinsight_pmpi,
        MPI_Init_begin,
        TP_ARGS(
            CODEPTR_ARG,
            char *, hostname,
            unsigned int,         pid
        ),
        TP_FIELDS(
            COMMON_TP_FIELDS_PMPI
            ctf_string(hostname, hostname)
            ctf_integer(unsigned int, pid, pid)
        )
)

TRACEPOINT_EVENT(
        lttng_pinsight_pmpi,
        MPI_Init_end,
        TP_ARGS(
            CODEPTR_ARG,
            int, return_value
        ),
        TP_FIELDS(
            COMMON_TP_FIELDS_PMPI
            ctf_integer(int, return_value, return_value)
        )
)

/* int MPI_Init_thread(int *argc, char ***argv, int required, int *provided) */
TRACEPOINT_EVENT(
        lttng_pinsight_pmpi,
        MPI_Init_thread_begin,
        TP_ARGS(
            CODEPTR_ARG,
            char *, hostname,
            unsigned int, pid,
            int, required
        ),
        TP_FIELDS(
            COMMON_TP_FIELDS_PMPI
            ctf_string(hostname, hostname)
            ctf_integer(unsigned int, pid, pid)
            ctf_integer(int, required, required)
        )
)

TRACEPOINT_EVENT(
        lttng_pinsight_pmpi,
        MPI_Init_thread_end,
        TP_ARGS(
            CODEPTR_ARG,
            int, provided,
            int, return_value
        ),
        TP_FIELDS(
            COMMON_TP_FIELDS_PMPI
            ctf_integer(int, provided, provided)
            ctf_integer(int, return_value, return_value)
        )
)

/**
 * For the tracepoints immediately after most PMPI_* call, we only need to record necessary info such as mpirank, codeptr,
 * and the return value of the PMPI call. Since those records have exactly same fields, we can use TRACEPOINT_EVENT_CLASS
 * and TRACEPOINT_EVENT_INSTANCE to define. TRACEPOINT_EVENT_CLASS is used to define a class of those events that have
 * the same record fields, and it unfortunatelly requires their TP_ARGS (tracepoint call arguments) are the same (limitation
 * of C preprocessing macros). Then TRACEPOINT_EVENT_INSTANCE can be used to define the TRACEPOINT_EVENT for a specific
 * event. When defining via TRACEPOINT_EVENT_INSTANCE, TP_FIELDS must be specified and to be exactly the same as
 * TRACEPOINT_EVENT_CLASS, and TP_FIELDS should be omitted.
 */
TRACEPOINT_EVENT_CLASS(
        lttng_pinsight_pmpi,
        MPI_Func_end_class,
        TP_ARGS(
            CODEPTR_ARG,
            int, return_value
        ),

        TP_FIELDS(
            COMMON_TP_FIELDS_PMPI
            ctf_integer(int, return_value, return_value)
        )
)

/**int MPI_Finalize(void) */
TRACEPOINT_EVENT(
        lttng_pinsight_pmpi,
        MPI_Finalize_begin,
        TP_ARGS(
            CODEPTR_ARG,
            int, useless_but_needed
        ),
        TP_FIELDS(
            COMMON_TP_FIELDS_PMPI
        )
)

TRACEPOINT_EVENT_INSTANCE(
        lttng_pinsight_pmpi,
        MPI_Func_end_class,
        MPI_Finalize_end,
        TP_ARGS(
            CODEPTR_ARG,
            int, return_value
        )
)

/* int MPI_Send(const void *buf, int count, MPI_Datatype datatype, int dest, int tag, MPI_Comm comm) */
TRACEPOINT_EVENT(
        lttng_pinsight_pmpi, MPI_Send_begin,
        TP_ARGS(
            CODEPTR_ARG,
            const void *,  buf,
            unsigned int,  count,
            unsigned int,  dest,
            unsigned int,  tag
        ),
        TP_FIELDS(
            COMMON_TP_FIELDS_PMPI
            ctf_integer_hex(unsigned int, buf, buf)
            ctf_integer(unsigned int, count, count)
            ctf_integer(unsigned int, dest, dest)
            ctf_integer(unsigned int, tag, tag)
        )
)

/**
 * use MPI_Func_end_class TRACEPOINT_EVENT_CLASS definition created before
 */
TRACEPOINT_EVENT_INSTANCE(
        lttng_pinsight_pmpi,
        MPI_Func_end_class,
        MPI_Send_end,
        TP_ARGS(
            CODEPTR_ARG,
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
TRACEPOINT_EVENT(
        lttng_pinsight_pmpi, MPI_Recv_begin,
        TP_ARGS(
            CODEPTR_ARG,
            const void *,  buf,
            unsigned int,  count,
            unsigned int,  source,
            unsigned int,  tag
        ),
        TP_FIELDS(
            COMMON_TP_FIELDS_PMPI
            ctf_integer_hex(unsigned int, buf, buf)
            ctf_integer(unsigned int, count, count)
            ctf_integer(unsigned int, source, source)
            ctf_integer(unsigned int, tag, tag)
        )
)

/** TODO: ideally, one may want the end tracepoint to record the real transfer status based on the status of the call */
TRACEPOINT_EVENT_INSTANCE(
        lttng_pinsight_pmpi,
        MPI_Func_end_class,
        MPI_Recv_end,
        TP_ARGS(
            CODEPTR_ARG,
            int, return_value
        )
)

/* int MPI_Barrier( MPI_Comm comm ) */
TRACEPOINT_EVENT(
        lttng_pinsight_pmpi, MPI_Barrier_begin,
        TP_ARGS(
            CODEPTR_ARG,
            int, useless_but_needed
        ),
        TP_FIELDS(
            COMMON_TP_FIELDS_PMPI
        )
)

TRACEPOINT_EVENT_INSTANCE(
        lttng_pinsight_pmpi,
        MPI_Func_end_class,
        MPI_Barrier_end,
        TP_ARGS(
            CODEPTR_ARG,
            int, return_value
        )
)

/* int MPI_Reduce(const void *sendbuf, void *recvbuf, int count, MPI_Datatype datatype,
               MPI_Op op, int root, MPI_Comm comm) */
TRACEPOINT_EVENT(
        lttng_pinsight_pmpi, MPI_Reduce_begin,
        TP_ARGS(
            CODEPTR_ARG,
            const void *,  sendbuf,
            const void *,  recvbuf,
            unsigned int,  count,
            unsigned int,  root,
            void *,  mpi_op
        ),
        TP_FIELDS(
            COMMON_TP_FIELDS_PMPI
            ctf_integer_hex(unsigned int, sendbuf, sendbuf)
            ctf_integer_hex(unsigned int, recvbuf, recvbuf)
            ctf_integer(unsigned int, count, count)
            ctf_integer(unsigned int, root, root)
            ctf_integer_hex(unsigned int, mpi_op, mpi_op)
        )
)

TRACEPOINT_EVENT_INSTANCE(
        lttng_pinsight_pmpi,
        MPI_Func_end_class,
        MPI_Reduce_end,
        TP_ARGS(
            CODEPTR_ARG,
            int, return_value
        )
)

/* int MPI_Allreduce(const void *sendbuf, void *recvbuf, int count,
                  MPI_Datatype datatype, MPI_Op op, MPI_Comm comm)*/
TRACEPOINT_EVENT(
        lttng_pinsight_pmpi, MPI_Allreduce_begin,
        TP_ARGS(
            CODEPTR_ARG,
            const void *,  sendbuf,
            const void *,  recvbuf,
            unsigned int,  count,
            void *,  mpi_op
        ),
        TP_FIELDS(
            COMMON_TP_FIELDS_PMPI
            ctf_integer_hex(unsigned int, sendbuf, sendbuf)
            ctf_integer_hex(unsigned int, recvbuf, recvbuf)
            ctf_integer(unsigned int, count, count)
            ctf_integer_hex(unsigned int, mpi_op, mpi_op)
        )
)

TRACEPOINT_EVENT_INSTANCE(
        lttng_pinsight_pmpi,
        MPI_Func_end_class,
        MPI_Allreduce_end,
        TP_ARGS(
            CODEPTR_ARG,
            int, return_value
        )
)

#ifdef __cplusplus
}
#endif

#endif /* _TRACEPOINT_PMPI_LTTNG_TRACEPOINT_H */

/* This part must be outside ifdef protection */
#include <lttng/tracepoint-event.h>
