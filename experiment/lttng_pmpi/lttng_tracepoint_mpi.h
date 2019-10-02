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
#define TRACEPOINT_PROVIDER lttng_pinsight_mpi

#undef TRACEPOINT_INCLUDE_FILE
#define TRACEPOINT_INCLUDE_FILE ./lttng_tracepoint_mpi.h

#if !defined(_TRACEPOINT_LTTNG_TRACEPOINT_MPI_H) || defined(TRACEPOINT_HEADER_MULTI_READ)
#define _TRACEPOINT_LTTNG_TRACEPOINT_MPI_H

#ifdef __cplusplus
extern "C" {
#endif

#include <lttng/tracepoint.h>
#include <stdbool.h>

/** Macros used to simplify the definition of LTTng TRACEPOINT_EVENT */
#define COMMON_TP_ARGS

//COMMON_TP_FIELDS are those fields in the thread-local storage. These fields will be added to all the trace records
#define COMMON_TP_FIELDS \
    ctf_integer(unsigned int, mpirank, mpirank)

/* mpi_init_begin/end: ideally, we want to uniquely identify a process before we know its MPI rank, e.g. hostname/IP + pid.
 * However, right now we just use pid */
TRACEPOINT_EVENT(
        lttng_pinsight_mpi,
        MPI_Init_begin,
        TP_ARGS(
            unsigned int,         pid
        ),
        TP_FIELDS(
            ctf_integer(unsigned int, pid, pid)
        )
)

TRACEPOINT_EVENT(
        lttng_pinsight_mpi,
        MPI_Init_end,
        TP_ARGS(
            unsigned int,  pid,
            unsigned int, mpirank
        ),
        TP_FIELDS(
            COMMON_TP_FIELDS
        )
)

/** MPI_finalize begin and end */

TRACEPOINT_EVENT(
        lttng_pinsight_mpi,
        MPI_Finalize_begin,
        TP_ARGS(
            unsigned int, mpirank
        ),
        TP_FIELDS(
            COMMON_TP_FIELDS
        )
)

TRACEPOINT_EVENT(
        lttng_pinsight_mpi,
        MPI_Finalize_end,
        TP_ARGS(
            unsigned int, mpirank
        ),
        TP_FIELDS(
            COMMON_TP_FIELDS
        )
)

/* MPI_Send */
#define TRACEPOINT_EVENT_MPI_SEND(event_name)                                    \
    TRACEPOINT_EVENT(                                                          \
        lttng_pinsight_mpi, event_name,                                            \
        TP_ARGS(                                                               \
            const void *,  buf,                                          \
            unsigned int,  count,                                          \
            unsigned int,  dest,                                          \
            unsigned int,  tag                                          \
        ),                                                                     \
        TP_FIELDS(                                                             \
            COMMON_TP_FIELDS                                                    \
            ctf_integer_hex(unsigned int, buf, buf)                  \
            ctf_integer(unsigned int, count, count)      \
            ctf_integer(unsigned int, dest, dest)      \
            ctf_integer(unsigned int, tag, tag)      \
        )                                                                      \
    )

TRACEPOINT_EVENT_MPI_SEND(MPI_Send_begin)
TRACEPOINT_EVENT_MPI_SEND(MPI_Send_end)

/* MPI_Recv, which is almost the same except dest->source */
#define TRACEPOINT_EVENT_MPI_RECV(event_name)                                    \
    TRACEPOINT_EVENT(                                                          \
        lttng_pinsight_mpi, event_name,                                            \
        TP_ARGS(                                                               \
            const void *,  buf,                                          \
            unsigned int,  count,                                          \
            unsigned int,  source,                                          \
            unsigned int,  tag                                          \
        ),                                                                     \
        TP_FIELDS(                                                             \
            COMMON_TP_FIELDS                                                    \
            ctf_integer_hex(unsigned int, buf, buf)                  \
            ctf_integer(unsigned int, count, count)      \
            ctf_integer(unsigned int, source, source)      \
            ctf_integer(unsigned int, tag, tag)      \
        )                                                                      \
    )

TRACEPOINT_EVENT_MPI_RECV(MPI_Recv_begin)
TRACEPOINT_EVENT_MPI_RECV(MPI_Recv_end)

/* MPI_Barrier */
#define TRACEPOINT_EVENT_MPI_BARRIER(event_name)                                    \
    TRACEPOINT_EVENT(                                                          \
        lttng_pinsight_mpi, event_name,                                            \
        TP_ARGS(                                                               \
            unsigned int,  useless                                          \
        ),                                                                     \
        TP_FIELDS(                                                             \
            COMMON_TP_FIELDS                                                    \
        )                                                                      \
    )

TRACEPOINT_EVENT_MPI_BARRIER(MPI_Barrier_begin)
TRACEPOINT_EVENT_MPI_BARRIER(MPI_Barrier_end)

/* MPI_Reduce(const void *sendbuf, void *recvbuf, int count, MPI_Datatype datatype,
               MPI_Op op, int root, MPI_Comm comm) */
#define TRACEPOINT_EVENT_MPI_REDUCE(event_name)                                    \
    TRACEPOINT_EVENT(                                                          \
        lttng_pinsight_mpi, event_name,                                            \
        TP_ARGS(                                                               \
            const void *,  sendbuf,                                          \
            const void *,  recvbuf,                                          \
            unsigned int,  count,                                          \
            unsigned int,  root,                                          \
            void *,  mpi_op                                          \
        ),                                                                     \
        TP_FIELDS(                                                             \
            COMMON_TP_FIELDS                                                    \
            ctf_integer_hex(unsigned int, sendbuf, sendbuf)                  \
            ctf_integer_hex(unsigned int, recvbuf, recvbuf)                  \
            ctf_integer(unsigned int, count, count)      \
            ctf_integer(unsigned int, root, root)      \
            ctf_integer_hex(unsigned int, mpi_op, mpi_op)      \
        )                                                                      \
    )

TRACEPOINT_EVENT_MPI_REDUCE(MPI_Reduce_begin)
TRACEPOINT_EVENT_MPI_REDUCE(MPI_Reduce_end)

/* int MPI_Allreduce(const void *sendbuf, void *recvbuf, int count,
                  MPI_Datatype datatype, MPI_Op op, MPI_Comm comm)*/
#define TRACEPOINT_EVENT_MPI_ALLREDUCE(event_name)                                    \
    TRACEPOINT_EVENT(                                                          \
        lttng_pinsight_mpi, event_name,                                            \
        TP_ARGS(                                                               \
            const void *,  sendbuf,                                          \
            const void *,  recvbuf,                                          \
            unsigned int,  count,                                          \
            void *,  mpi_op                                          \
        ),                                                                     \
        TP_FIELDS(                                                             \
            COMMON_TP_FIELDS                                                    \
            ctf_integer_hex(unsigned int, sendbuf, sendbuf)                  \
            ctf_integer_hex(unsigned int, recvbuf, recvbuf)                  \
            ctf_integer(unsigned int, count, count)      \
            ctf_integer_hex(unsigned int, mpi_op, mpi_op)      \
        )                                                                      \
    )

TRACEPOINT_EVENT_MPI_ALLREDUCE(MPI_Allreduce_begin)
TRACEPOINT_EVENT_MPI_ALLREDUCE(MPI_Allreduce_end)


#ifdef __cplusplus
}
#endif

#endif /* _TRACEPOINT_LTTNG_TRACEPOINT_MPI_H */

/* This part must be outside ifdef protection */
#include <lttng/tracepoint-event.h>