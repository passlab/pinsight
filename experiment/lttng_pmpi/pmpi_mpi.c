/**
 *
 * Copyright (c) Yonghong Yan (yanyh15@ github or gamil) and PASSLab (http://passlab.github.io/). All rights reserved.
 *
 * This file is subject to the terms and conditions defined in
 * file 'LICENSE.txt', which is part of this source code package.
 *
 * MPI APIs with PMPI and LTTng UST tracepoint inserted.
 *
 */
#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#ifndef _EXTERN_C_
#ifdef __cplusplus
#define _EXTERN_C_ extern "C"
#else /* __cplusplus */
#define _EXTERN_C_
#endif /* __cplusplus */
#endif /* _EXTERN_C_ */

int mpirank = -1;

#define TRACEPOINT_CREATE_PROBES
#define TRACEPOINT_DEFINE
#include "lttng_tracepoint_mpi.h"

/* ================== C Wrappers for MPI_Init ================== */
_EXTERN_C_ int PMPI_Init(int *argc, char ***argv);
_EXTERN_C_ int MPI_Init(int *argc, char ***argv) {
    unsigned int pid = getpid();
    tracepoint(lttng_pinsight_mpi, MPI_Init_begin, pid);
    int return_val = PMPI_Init(argc, argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &mpirank);
    tracepoint(lttng_pinsight_mpi, MPI_Init_end, pid, mpirank);
    return return_val;
}

/* ================== C Wrappers for MPI_Finalize ================== */
_EXTERN_C_ int PMPI_Finalize();
_EXTERN_C_ int MPI_Finalize() {
    tracepoint(lttng_pinsight_mpi, MPI_Finalize_begin, mpirank);
    int return_val = PMPI_Finalize();
    tracepoint(lttng_pinsight_mpi, MPI_Finalize_end, mpirank);
    return return_val;
}


/* ================== C Wrappers for MPI_Send ================== */
_EXTERN_C_ int PMPI_Send(const void *buf, int count, MPI_Datatype datatype, int dest, int tag, MPI_Comm comm);
_EXTERN_C_ int MPI_Send(const void *buf, int count, MPI_Datatype datatype, int dest, int tag, MPI_Comm comm) {
    tracepoint(lttng_pinsight_mpi, MPI_Send_begin, buf, count, dest, tag);
    int return_val = PMPI_Send(buf, count, datatype, dest, tag, comm);
    tracepoint(lttng_pinsight_mpi, MPI_Send_end, buf, count, dest, tag);
    return return_val;
}

/* ================== C Wrappers for MPI_Recv ================== */
_EXTERN_C_ int PMPI_Recv(void *buf, int count, MPI_Datatype datatype, int source, int tag, MPI_Comm comm, MPI_Status *status);
_EXTERN_C_ int MPI_Recv(void *buf, int count, MPI_Datatype datatype, int source, int tag, MPI_Comm comm, MPI_Status *status) {
    tracepoint(lttng_pinsight_mpi, MPI_Recv_begin, buf, count, source, tag);
    int return_val = PMPI_Recv(buf, count, datatype, source, tag, comm, status);
    tracepoint(lttng_pinsight_mpi, MPI_Recv_end, buf, count, source, tag);
    return return_val;
}

/* ================== C Wrappers for MPI_Barrier ================== */
_EXTERN_C_ int PMPI_Barrier(MPI_Comm comm);
_EXTERN_C_ int MPI_Barrier(MPI_Comm comm) {
    tracepoint(lttng_pinsight_mpi, MPI_Barrier_begin, 0);
    int return_val = PMPI_Barrier(comm);
    tracepoint(lttng_pinsight_mpi, MPI_Barrier_end, 0);
    return return_val;
}

/* ================== C Wrappers for MPI_Reduce ================== */
_EXTERN_C_ int PMPI_Reduce(const void *sendbuf, void *recvbuf, int count, MPI_Datatype datatype, MPI_Op op, int root, MPI_Comm comm);
_EXTERN_C_ int MPI_Reduce(const void *sendbuf, void *recvbuf, int count, MPI_Datatype datatype, MPI_Op op, int root, MPI_Comm comm) {
    tracepoint(lttng_pinsight_mpi, MPI_Reduce_begin, sendbuf, recvbuf, count, root, (void*)op);
    int return_val = PMPI_Reduce(sendbuf, recvbuf, count, datatype, op, root, comm);
    tracepoint(lttng_pinsight_mpi, MPI_Reduce_end, sendbuf, recvbuf, count, root, (void*)op);
    return return_val;
}

/* ================== C Wrappers for MPI_Allreduce ================== */
_EXTERN_C_ int PMPI_Allreduce(const void *sendbuf, void *recvbuf, int count, MPI_Datatype datatype, MPI_Op op, MPI_Comm comm);
_EXTERN_C_ int MPI_Allreduce(const void *sendbuf, void *recvbuf, int count, MPI_Datatype datatype, MPI_Op op, MPI_Comm comm) {
    tracepoint(lttng_pinsight_mpi, MPI_Allreduce_begin, sendbuf, recvbuf, count, (void*) op);
    int return_val = PMPI_Allreduce(sendbuf, recvbuf, count, datatype, op, comm);
    tracepoint(lttng_pinsight_mpi, MPI_Allreduce_end, sendbuf, recvbuf, count, (void*)op);
    return return_val;
}


