/**
 *
 * Copyright (c) Yonghong Yan (yanyh15@ github or gamil) and PASSLab
 * (http://passlab.github.io/). All rights reserved.
 *
 * This file is subject to the terms and conditions defined in
 * file 'LICENSE.txt', which is part of this source code package.
 *
 * MPI APIs with PMPI and LTTng UST tracepoint inserted.
 *
 */
#include "pinsight.h"
#include "pinsight_control_thread.h"
#include "trace_config.h"
#include "trace_domain_MPI.h"
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

int mpirank = 0;
__thread void *mpi_codeptr = NULL;
int MPI_domain_index;
domain_info_t *MPI_domain_info;
domain_trace_config_t *MPI_trace_config;

#define LTTNG_UST_TRACEPOINT_CREATE_PROBES
#define LTTNG_UST_TRACEPOINT_DEFINE
#include "pmpi_lttng_ust_tracepoint.h"

/**
 * The idea to add an MPI method to the tracing framework of LTTng is via PMPI
 * interface. Each an MPI method is implemented using the corresponding PMPI
 * method. In general, before and after the PMPI call, we use LTTng-UST
 * tracepoint to record the MPI call. Each MPI call record contains myrank (the
 * MPI rank), codeptr (the address in the user code of the MPI call), and MPI
 * method-specific fields. To facilitate runtime bookkeeping and to optimize
 * tracing frequency, an MPI call and its address location (lexgion) are kept
 * track in the runtime system so tracing can be turned on and off based on
 * user-provided tracing rate (check README.md file).
 *
 * The implemention (in this file pmpi_mpi.c and pmpi_lttng_tracepoint.h file)
 * uses two preprocesing macros (PMPI_CALL_PROLOGUE and PMPI_CALL_EPILOGUE), and
 * LTTng TRACEPOINT_EVENT definitions are also macro based. Those macros are
 * refactored nicely in such a way that using them is pretty straightforward by
 * following the examples. Their implementations are however complicated to
 * understand, But I hope most of those who will develop based on these files do
 * not need to know that.
 *
 * To add an MPI method, e.g. MPI_Foo, to the tracing framework:
 * 1. Add to the MPI_LEXGION_type_t enum an entry named MPI_Foo_LEXGION, must be
 * exactly this name because of the way it is used in the PMPI_CALL_PROLOGUE and
 * PMPI_CALL_EPILOGUE macro
 * 2. Define LTTng-UST TRACEPOINT_EVENTs in pmpi_lttng_tracepoint.h file for the
 * MPI_Foo, using LTTng-UST TRACEPOINT_EVENT-related macro. The two events
 * should be named as MPI_Foo_begin and MPI_Foo_end, representing the event
 * before the MPI_Foo call and after the call. In the definition, the tracepoint
 * call arguments (TP_ARGS), and the event record fields (TP_FIELDS) should be
 * selected and defined based on the parameters of the MPI_Foo call itself.
 * First, decide what fields are needed to be recorded in a trace record, and
 * then decide what arguments are needed to pass to the trace recording. To work
 * with the PMPI_CALL_PROLOGUE and PMPI_CALL_EPILOGUE macro, the first TP_ARGS
 * argument MUST be CODEPTR_ARG (codeptr), and the first TP_FIELDS field MUST be
 * COMMON_TP_FIELDS_PMPI (includes mpirank and codeptr). The use of
 * TRACEPOINT_EVENT_INSTANCE for defining MPI_Foo_end TRACEPOINT_EVENT is
 * optional. It is used with the definition of TRACEPOINT_EVENT_CLASS that is
 * created for most the MPI_*_end events (the end events for most MPI methods).
 *    Because most MPI_*_end events only need to record COMMON_TP_FIELDS_PMPI
 * and a return value of the corresponding PMPI call, we can use LTTng-UST
 * TRACEPOINT_EVENT_CLASS feature to define a single event for all those records
 * that have the same fields. After this step, it is important to note the
 * TP_ARGS definition of those arguments that will be passed to the tracepoint
 * call via PMPI_CALL_PROLOGUE
 *
 * 3. In this file, create the MPI_Foo implementation using the corresponding
 * PMPI_Foo function, and then add calls to PMPI_CALL_PROLOGUE and
 * PMPI_CALL_EPILOGUE macros before and after the PMPI_Foo call. The arguments
 * passed to the two macros calls should match the parameters of the TP_ARGS
 * arguments (not including CODEPTR_ARG) of the TRACEPOINT_EVENT definition for
 * MPI_Foo_begin and MPI_Foo_end.
 *
 * By following the above steps, it should be straightforward to add an MPI
 * method to the tracing framework.
 */

/**
 * enum to define code for each MPI methods, used as one of the key of lexgion
 * directory maintained by the runtime.
 */
typedef enum MPI_LEXGION_type {
  MPI_Init_LEXGION = 0,
  MPI_Init_thread_LEXGION,
  MPI_Finalize_LEXGION,
  MPI_Send_LEXGION,
  MPI_Recv_LEXGION,
  MPI_Sendrecv_LEXGION,
  MPI_Isend_LEXGION,
  MPI_Irecv_LEXGION,
  MPI_Wait_LEXGION,
  MPI_Waitall_LEXGION,
  MPI_Bcast_LEXGION,
  MPI_Barrier_LEXGION,
  MPI_Reduce_LEXGION,
  MPI_Allreduce_LEXGION,
  MPI_Scatter_LEXGION,
  MPI_Gather_LEXGION,
  MPI_Allgather_LEXGION,
  MPI_Alltoall_LEXGION,
} MPI_LEXGION_type_t;

int MPI_get_rank(void) {
  int rank;
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  return rank;
}

/**
 * This two macros are complicatedly refactored so we can have the code for
 * MPI_* implemented cleanly. The two macros must be used together for each MPI
 * function. The first argument of the macro is the MPI function name, and the
 * rest arguments are basically the parameters of the MPI call. Those arguments
 * will be passed to lttng tracepoint call and recorded.
 *
 * Those parameters needs to coordinated with the lttng MPI tracepoint
 * definition in pmpi_lttng_tracepoint.h
 *
 * codeptr that is used by all MPI tracepoints are handled implicitly in this
 * macro. mpirank that is recorded in a trace record is passed to tracepoint
 * fields from a global variable.
 */
/* ================================================================
 * PMPI_CALL_PROLOGUE / PMPI_CALL_EPILOGUE
 *
 * Pattern mirrors the OMPT and CUDA callback designs:
 *   1. Domain killswitch: if MPI domain is OFF, skip lexgion and tracepoint
 *      entirely (lgp stays NULL; epilogue guards on lgp).
 *   2. Rate control: lexgion_set_top_trace_bit_domain_event decides whether
 *      to fire the tracepoint for this invocation.
 *
 * Configuration reloading and mode switching are handled by the dedicated
 * control thread (pinsight_control_thread.c). Mode flags are volatile —
 * reads here see the latest value written by the control thread.
 * ================================================================ */
#define PMPI_CALL_PROLOGUE(MPI_FUNC, ...)                                      \
  pinsight_check_pause(); /* cooperative pause for introspection */            \
  /* OFF/STANDBY killswitch: lgp = NULL signals epilogue to skip entirely */   \
  lexgion_t *lgp = NULL;                                                       \
  if (PINSIGHT_DOMAIN_ACTIVE(                                                  \
          domain_default_trace_config[MPI_domain_index].mode)) {               \
    mpi_codeptr = __builtin_return_address(0);                                 \
    lexgion_record_t *_record =                                                \
        lexgion_begin(MPI_LEXGION, MPI_FUNC##_LEXGION, mpi_codeptr);           \
    lgp = _record->lgp;                                                        \
    /* Rate decision and LTTng only in TRACING mode */                         \
    if (PINSIGHT_SHOULD_TRACE(MPI_domain_index)) {                             \
      lexgion_set_top_trace_bit_domain_event(lgp, MPI_domain_index,            \
                                             MPI_FUNC##_LEXGION);              \
      if (lgp->trace_bit) {                                                    \
        lttng_ust_tracepoint(pmpi_pinsight_lttng_ust, MPI_FUNC##_begin,        \
                             __VA_ARGS__);                                     \
      }                                                                        \
    }                                                                          \
  }

#define PMPI_CALL_EPILOGUE(MPI_FUNC, ...)                                      \
  if (lgp) {                                                                   \
    lexgion_end(NULL);                                                         \
    lgp->end_codeptr_ra = mpi_codeptr;                                         \
    if (PINSIGHT_SHOULD_TRACE(MPI_domain_index) && lgp->trace_bit) {           \
      lttng_ust_tracepoint(pmpi_pinsight_lttng_ust, MPI_FUNC##_end,            \
                           __VA_ARGS__);                                       \
      lexgion_post_trace_update(lgp);                                          \
    }                                                                          \
  }

_EXTERN_C_ int PMPI_Init_thread(int *argc, char ***argv, int required,
                                int *provided);
/* ================== C Wrappers for MPI_Init ================== */
_EXTERN_C_ int MPI_Init_thread(int *argc, char ***argv, int required,
                               int *provided) {
  PMPI_CALL_PROLOGUE(MPI_Init_thread, required);

  int return_val = PMPI_Init_thread(argc, argv, required, provided);
  PMPI_Comm_rank(MPI_COMM_WORLD, &mpirank);
  // printf("process %d rank: %d\n", pid, mpirank);

  PMPI_CALL_EPILOGUE(MPI_Init_thread, *provided, return_val);

  return return_val;
}

/* ================== C Wrappers for MPI_Init ================== */
_EXTERN_C_ int PMPI_Init(int *argc, char ***argv);
_EXTERN_C_ int MPI_Init(int *argc, char ***argv) {
  PMPI_CALL_PROLOGUE(MPI_Init, 0);
  int return_val = PMPI_Init(argc, argv);
  PMPI_Comm_rank(MPI_COMM_WORLD, &mpirank);
  PMPI_CALL_EPILOGUE(MPI_Init, return_val);

  // printf("process %d rank: %d\n", pid, mpirank);
  return return_val;
}

/* ================== C Wrappers for MPI_Finalize ================== */
_EXTERN_C_ int PMPI_Finalize();
_EXTERN_C_ int MPI_Finalize() {
  PMPI_CALL_PROLOGUE(MPI_Finalize, 0);
  int return_val = PMPI_Finalize();
  PMPI_CALL_EPILOGUE(MPI_Finalize, return_val);
  return return_val;
}

/* ================== C Wrappers for MPI_Send ================== */
_EXTERN_C_ int PMPI_Send(const void *buf, int count, MPI_Datatype datatype,
                         int dest, int tag, MPI_Comm comm);
_EXTERN_C_ int MPI_Send(const void *buf, int count, MPI_Datatype datatype,
                        int dest, int tag, MPI_Comm comm) {
  PMPI_CALL_PROLOGUE(MPI_Send, buf, count, dest, tag);
  int return_val = PMPI_Send(buf, count, datatype, dest, tag, comm);
  PMPI_CALL_EPILOGUE(MPI_Send, return_val);

  return return_val;
}

/* ================== C Wrappers for MPI_Recv ================== */
_EXTERN_C_ int PMPI_Recv(void *buf, int count, MPI_Datatype datatype,
                         int source, int tag, MPI_Comm comm,
                         MPI_Status *status);
_EXTERN_C_ int MPI_Recv(void *buf, int count, MPI_Datatype datatype, int source,
                        int tag, MPI_Comm comm, MPI_Status *status) {
  PMPI_CALL_PROLOGUE(MPI_Recv, buf, count, source, tag);
  int return_val = PMPI_Recv(buf, count, datatype, source, tag, comm, status);
  PMPI_CALL_EPILOGUE(MPI_Recv, return_val);
  return return_val;
}

/* ================== C Wrappers for MPI_Barrier ================== */
_EXTERN_C_ int PMPI_Barrier(MPI_Comm comm);
_EXTERN_C_ int MPI_Barrier(MPI_Comm comm) {
  PMPI_CALL_PROLOGUE(MPI_Barrier, 0);
  int return_val = PMPI_Barrier(comm);
  PMPI_CALL_EPILOGUE(MPI_Barrier, return_val);
  return return_val;
}

/* ================== C Wrappers for MPI_Reduce ================== */
_EXTERN_C_ int PMPI_Reduce(const void *sendbuf, void *recvbuf, int count,
                           MPI_Datatype datatype, MPI_Op op, int root,
                           MPI_Comm comm);
_EXTERN_C_ int MPI_Reduce(const void *sendbuf, void *recvbuf, int count,
                          MPI_Datatype datatype, MPI_Op op, int root,
                          MPI_Comm comm) {
  PMPI_CALL_PROLOGUE(MPI_Reduce, sendbuf, recvbuf, count, root, (void *)op);
  int return_val =
      PMPI_Reduce(sendbuf, recvbuf, count, datatype, op, root, comm);
  PMPI_CALL_EPILOGUE(MPI_Reduce, return_val);
  return return_val;
}

/* ================== C Wrappers for MPI_Allreduce ================== */
_EXTERN_C_ int PMPI_Allreduce(const void *sendbuf, void *recvbuf, int count,
                              MPI_Datatype datatype, MPI_Op op, MPI_Comm comm);
_EXTERN_C_ int MPI_Allreduce(const void *sendbuf, void *recvbuf, int count,
                             MPI_Datatype datatype, MPI_Op op, MPI_Comm comm) {
  PMPI_CALL_PROLOGUE(MPI_Allreduce, sendbuf, recvbuf, count, (void *)op);
  int return_val = PMPI_Allreduce(sendbuf, recvbuf, count, datatype, op, comm);
  PMPI_CALL_EPILOGUE(MPI_Allreduce, return_val);
  return return_val;
}

/* ================== C Wrappers for MPI_Bcast ================== */
_EXTERN_C_ int PMPI_Bcast(void *buffer, int count, MPI_Datatype datatype,
                          int root, MPI_Comm comm);
_EXTERN_C_ int MPI_Bcast(void *buffer, int count, MPI_Datatype datatype,
                         int root, MPI_Comm comm) {
  PMPI_CALL_PROLOGUE(MPI_Bcast, buffer, count, root);
  int return_val = PMPI_Bcast(buffer, count, datatype, root, comm);
  PMPI_CALL_EPILOGUE(MPI_Bcast, return_val);
  return return_val;
}

/* ================== C Wrappers for MPI_Sendrecv ================== */
_EXTERN_C_ int PMPI_Sendrecv(const void *sendbuf, int sendcount,
                             MPI_Datatype sendtype, int dest, int sendtag,
                             void *recvbuf, int recvcount,
                             MPI_Datatype recvtype, int source, int recvtag,
                             MPI_Comm comm, MPI_Status *status);
_EXTERN_C_ int MPI_Sendrecv(const void *sendbuf, int sendcount,
                            MPI_Datatype sendtype, int dest, int sendtag,
                            void *recvbuf, int recvcount, MPI_Datatype recvtype,
                            int source, int recvtag, MPI_Comm comm,
                            MPI_Status *status) {
  PMPI_CALL_PROLOGUE(MPI_Sendrecv, sendbuf, sendcount, dest, sendtag, recvbuf,
                     recvcount, source, recvtag);
  int return_val =
      PMPI_Sendrecv(sendbuf, sendcount, sendtype, dest, sendtag, recvbuf,
                    recvcount, recvtype, source, recvtag, comm, status);
  PMPI_CALL_EPILOGUE(MPI_Sendrecv, return_val);
  return return_val;
}

/* ================== C Wrappers for MPI_Isend ================== */
_EXTERN_C_ int PMPI_Isend(const void *buf, int count, MPI_Datatype datatype,
                          int dest, int tag, MPI_Comm comm,
                          MPI_Request *request);
_EXTERN_C_ int MPI_Isend(const void *buf, int count, MPI_Datatype datatype,
                         int dest, int tag, MPI_Comm comm,
                         MPI_Request *request) {
  PMPI_CALL_PROLOGUE(MPI_Isend, buf, count, dest, tag);
  int return_val = PMPI_Isend(buf, count, datatype, dest, tag, comm, request);
  PMPI_CALL_EPILOGUE(MPI_Isend, return_val);
  return return_val;
}

/* ================== C Wrappers for MPI_Irecv ================== */
_EXTERN_C_ int PMPI_Irecv(void *buf, int count, MPI_Datatype datatype,
                          int source, int tag, MPI_Comm comm,
                          MPI_Request *request);
_EXTERN_C_ int MPI_Irecv(void *buf, int count, MPI_Datatype datatype,
                         int source, int tag, MPI_Comm comm,
                         MPI_Request *request) {
  PMPI_CALL_PROLOGUE(MPI_Irecv, buf, count, source, tag);
  int return_val = PMPI_Irecv(buf, count, datatype, source, tag, comm, request);
  PMPI_CALL_EPILOGUE(MPI_Irecv, return_val);
  return return_val;
}

/* ================== C Wrappers for MPI_Wait ================== */
_EXTERN_C_ int PMPI_Wait(MPI_Request *request, MPI_Status *status);
_EXTERN_C_ int MPI_Wait(MPI_Request *request, MPI_Status *status) {
  PMPI_CALL_PROLOGUE(MPI_Wait, 0);
  int return_val = PMPI_Wait(request, status);
  PMPI_CALL_EPILOGUE(MPI_Wait, return_val);
  return return_val;
}

/* ================== C Wrappers for MPI_Waitall ================== */
_EXTERN_C_ int PMPI_Waitall(int count, MPI_Request array_of_requests[],
                            MPI_Status array_of_statuses[]);
_EXTERN_C_ int MPI_Waitall(int count, MPI_Request array_of_requests[],
                           MPI_Status array_of_statuses[]) {
  PMPI_CALL_PROLOGUE(MPI_Waitall, count);
  int return_val = PMPI_Waitall(count, array_of_requests, array_of_statuses);
  PMPI_CALL_EPILOGUE(MPI_Waitall, return_val);
  return return_val;
}

/* ================== C Wrappers for MPI_Scatter ================== */
_EXTERN_C_ int PMPI_Scatter(const void *sendbuf, int sendcount,
                            MPI_Datatype sendtype, void *recvbuf, int recvcount,
                            MPI_Datatype recvtype, int root, MPI_Comm comm);
_EXTERN_C_ int MPI_Scatter(const void *sendbuf, int sendcount,
                           MPI_Datatype sendtype, void *recvbuf, int recvcount,
                           MPI_Datatype recvtype, int root, MPI_Comm comm) {
  PMPI_CALL_PROLOGUE(MPI_Scatter, sendbuf, sendcount, recvbuf, recvcount, root);
  int return_val = PMPI_Scatter(sendbuf, sendcount, sendtype, recvbuf,
                                recvcount, recvtype, root, comm);
  PMPI_CALL_EPILOGUE(MPI_Scatter, return_val);
  return return_val;
}

/* ================== C Wrappers for MPI_Gather ================== */
_EXTERN_C_ int PMPI_Gather(const void *sendbuf, int sendcount,
                           MPI_Datatype sendtype, void *recvbuf, int recvcount,
                           MPI_Datatype recvtype, int root, MPI_Comm comm);
_EXTERN_C_ int MPI_Gather(const void *sendbuf, int sendcount,
                          MPI_Datatype sendtype, void *recvbuf, int recvcount,
                          MPI_Datatype recvtype, int root, MPI_Comm comm) {
  PMPI_CALL_PROLOGUE(MPI_Gather, sendbuf, sendcount, recvbuf, recvcount, root);
  int return_val = PMPI_Gather(sendbuf, sendcount, sendtype, recvbuf, recvcount,
                               recvtype, root, comm);
  PMPI_CALL_EPILOGUE(MPI_Gather, return_val);
  return return_val;
}

/* ================== C Wrappers for MPI_Allgather ================== */
_EXTERN_C_ int PMPI_Allgather(const void *sendbuf, int sendcount,
                              MPI_Datatype sendtype, void *recvbuf,
                              int recvcount, MPI_Datatype recvtype,
                              MPI_Comm comm);
_EXTERN_C_ int MPI_Allgather(const void *sendbuf, int sendcount,
                             MPI_Datatype sendtype, void *recvbuf,
                             int recvcount, MPI_Datatype recvtype,
                             MPI_Comm comm) {
  PMPI_CALL_PROLOGUE(MPI_Allgather, sendbuf, sendcount, recvbuf, recvcount);
  int return_val = PMPI_Allgather(sendbuf, sendcount, sendtype, recvbuf,
                                  recvcount, recvtype, comm);
  PMPI_CALL_EPILOGUE(MPI_Allgather, return_val);
  return return_val;
}

/* ================== C Wrappers for MPI_Alltoall ================== */
_EXTERN_C_ int PMPI_Alltoall(const void *sendbuf, int sendcount,
                             MPI_Datatype sendtype, void *recvbuf,
                             int recvcount, MPI_Datatype recvtype,
                             MPI_Comm comm);
_EXTERN_C_ int MPI_Alltoall(const void *sendbuf, int sendcount,
                            MPI_Datatype sendtype, void *recvbuf, int recvcount,
                            MPI_Datatype recvtype, MPI_Comm comm) {
  PMPI_CALL_PROLOGUE(MPI_Alltoall, sendbuf, sendcount, recvbuf, recvcount);
  int return_val = PMPI_Alltoall(sendbuf, sendcount, sendtype, recvbuf,
                                 recvcount, recvtype, comm);
  PMPI_CALL_EPILOGUE(MPI_Alltoall, return_val);
  return return_val;
}
