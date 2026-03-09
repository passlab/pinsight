#ifndef MPI_DOMAIN_H
#define MPI_DOMAIN_H

extern int MPI_get_rank(void); // function to get the MPI rank

#include "trace_config.h"
#include "trace_domain_dsl.h"
#include "trace_domain_loader.h"

/* Provided by your core implementation (e.g. trace_domain_loader.c) */
extern struct domain_info domain_info_table[];
extern int num_domain;
extern int MPI_domain_index;
extern domain_info_t *MPI_domain_info;
extern domain_trace_config_t *MPI_trace_config;

/* --- 1. DSL BLOCK: MPI domain definition (data only) --- */

#define MPI_DOMAIN_DEFINITION                                                  \
  TRACE_DOMAIN_BEGIN("MPI", TRACE_EVENT_ID_NATIVE)                             \
                                                                               \
  /* [MPI.rank(0-127)] */                                                      \
  TRACE_PUNIT("rank", 0, 127, MPI_get_rank)                                    \
                                                                               \
  /* [MPI(init)] */                                                            \
  TRACE_SUBDOMAIN_BEGIN("init")                                                \
  TRACE_EVENT("MPI_Init", 1, 0, NULL)                                          \
  TRACE_EVENT("MPI_Init_thread", 1, 1, NULL)                                   \
  TRACE_EVENT("MPI_Finalize", 1, 2, NULL)                                      \
  TRACE_SUBDOMAIN_END()                                                        \
                                                                               \
  /* [MPI(p2p)] */                                                             \
  TRACE_SUBDOMAIN_BEGIN("p2p")                                                 \
  TRACE_EVENT("MPI_Send", 1, 3, NULL)                                          \
  TRACE_EVENT("MPI_Recv", 1, 4, NULL)                                          \
  TRACE_EVENT("MPI_Sendrecv", 0, 5, NULL)                                      \
  TRACE_SUBDOMAIN_END()                                                        \
                                                                               \
  /* [MPI(asyncp2p)] */                                                        \
  TRACE_SUBDOMAIN_BEGIN("asyncp2p")                                            \
  TRACE_EVENT("MPI_Isend", 0, 6, NULL)                                         \
  TRACE_EVENT("MPI_Irecv", 0, 7, NULL)                                         \
  TRACE_EVENT("MPI_Wait", 0, 8, NULL)                                          \
  TRACE_EVENT("MPI_Waitall", 0, 9, NULL)                                       \
  TRACE_EVENT("MPI_Test", 0, 10, NULL)                                         \
  TRACE_SUBDOMAIN_END()                                                        \
                                                                               \
  /* [MPI(collective)] */                                                      \
  TRACE_SUBDOMAIN_BEGIN("collective")                                          \
  TRACE_EVENT("MPI_Bcast", 0, 11, NULL)                                        \
  TRACE_EVENT("MPI_Barrier", 1, 12, NULL)                                      \
  TRACE_EVENT("MPI_Reduce", 0, 13, NULL)                                       \
  TRACE_EVENT("MPI_Allreduce", 0, 14, NULL)                                    \
  TRACE_EVENT("MPI_Scatter", 0, 15, NULL)                                      \
  TRACE_EVENT("MPI_Gather", 0, 16, NULL)                                       \
  TRACE_EVENT("MPI_Allgather", 0, 17, NULL)                                    \
  TRACE_EVENT("MPI_Alltoall", 0, 18, NULL)                                     \
  TRACE_SUBDOMAIN_END()                                                        \
                                                                               \
  /* [MPI(others)] */                                                          \
  TRACE_SUBDOMAIN_BEGIN("others")                                              \
  TRACE_EVENT("MPI_Comm_rank", 0, 19, NULL)                                    \
  TRACE_EVENT("MPI_Comm_size", 0, 20, NULL)                                    \
  TRACE_EVENT("MPI_Comm_split", 0, 21, NULL)                                   \
  TRACE_EVENT("MPI_Comm_create", 0, 22, NULL)                                  \
  TRACE_EVENT("MPI_Comm_dup", 0, 23, NULL)                                     \
                                                                               \
  TRACE_EVENT("MPI_Type_commit", 0, 24, NULL)                                  \
  TRACE_EVENT("MPI_Type_free", 0, 25, NULL)                                    \
                                                                               \
  TRACE_EVENT("MPI_Wtime", 0, 26, NULL)                                        \
  TRACE_EVENT("MPI_Wtick", 0, 27, NULL)                                        \
  TRACE_SUBDOMAIN_END()                                                        \
                                                                               \
  /* [MPI(oneside)] */                                                         \
  TRACE_SUBDOMAIN_BEGIN("oneside")                                             \
  TRACE_EVENT("MPI_Win_create", 0, 28, NULL)                                   \
  TRACE_EVENT("MPI_Win_lock", 0, 29, NULL)                                     \
  TRACE_EVENT("MPI_Win_unlock", 0, 30, NULL)                                   \
  TRACE_EVENT("MPI_Put", 0, 31, NULL)                                          \
  TRACE_EVENT("MPI_Get", 0, 32, NULL)                                          \
  TRACE_EVENT("MPI_Accumulate", 0, 33, NULL)                                   \
  TRACE_SUBDOMAIN_END()                                                        \
                                                                               \
  /* [MPI(io)] */                                                              \
  TRACE_SUBDOMAIN_BEGIN("io")                                                  \
  TRACE_EVENT("MPI_File_open", 0, 34, NULL)                                    \
  TRACE_EVENT("MPI_File_close", 0, 35, NULL)                                   \
  TRACE_EVENT("MPI_File_read", 0, 36, NULL)                                    \
  TRACE_EVENT("MPI_File_write", 0, 37, NULL)                                   \
  TRACE_SUBDOMAIN_END()                                                        \
                                                                               \
  TRACE_DOMAIN_END()

/* --- 2. Registration function (returns pointer to this MPI domain) --- */

static inline struct domain_info *register_MPI_trace_domain(void) {
  MPI_domain_index = num_domain; /* assign to global, declared in pmpi_mpi.c */

  /* Bind DSL macros to the generic helpers */

#define TRACE_IMPL_DOMAIN_BEGIN(name, mode)                                    \
  do {                                                                         \
    dsl_add_domain((name), (mode));

#define TRACE_IMPL_DOMAIN_END()                                                \
  }                                                                            \
  while (0)

#define TRACE_IMPL_PUNIT(name, low, high, punit_id_func, arg, num_arg)         \
  dsl_add_punit((name), (low), (high), ((int (*)())(punit_id_func)), (arg),    \
                (num_arg));

#define TRACE_IMPL_SUBDOMAIN_BEGIN(name)                                       \
  {                                                                            \
    dsl_add_subdomain((name));

#define TRACE_IMPL_SUBDOMAIN_END() }

#define TRACE_IMPL_EVENT(name, initial_status, native_id, callback_fn)         \
  dsl_add_event((name), (initial_status), (native_id), (void *)(callback_fn));

  /* Expand the MPI definition into actual calls */
  MPI_DOMAIN_DEFINITION;

/* Cleanup macro namespace */
#undef TRACE_IMPL_EVENT
#undef TRACE_IMPL_SUBDOMAIN_END
#undef TRACE_IMPL_SUBDOMAIN_BEGIN
#undef TRACE_IMPL_PUNIT
#undef TRACE_IMPL_DOMAIN_END
#undef TRACE_IMPL_DOMAIN_BEGIN

  /* Return pointer to this domain’s entry */
  MPI_domain_info = &domain_info_table[MPI_domain_index];
  MPI_trace_config = &domain_default_trace_config[MPI_domain_index];
  return MPI_domain_info;
}

#endif /* MPI_DOMAIN_H */
