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

/* --- 1. DSL BLOCK: MPI domain definition (data only) ---
 *
 * Implementation status:
 *   ✓ = PMPI wrapper + LTTng tracepoint implemented in pmpi_mpi.c
 *   - = event listed in config/DSL but no PMPI wrapper yet
 *
 * The native_id in TRACE_EVENT(..., native_id, ...) is a stable integer key
 * used by the rate-control and config subsystems only; it does NOT correspond
 * to any CUPTI or MPI enum value.
 */

#define MPI_DOMAIN_DEFINITION                                                  \
  TRACE_DOMAIN_BEGIN("MPI", TRACE_EVENT_ID_NATIVE)                             \
                                                                               \
  /* [MPI.rank(0-127)] */                                                      \
  TRACE_PUNIT("rank", 0, 127, MPI_get_rank)                                    \
                                                                               \
  /* [MPI(init)] — lifecycle, always on by default */                          \
  TRACE_SUBDOMAIN_BEGIN("init")                                                \
  TRACE_EVENT("MPI_Init",        1, 0, NULL)  /* ✓ implemented */             \
  TRACE_EVENT("MPI_Init_thread", 1, 1, NULL)  /* ✓ implemented */             \
  TRACE_EVENT("MPI_Finalize",    1, 2, NULL)  /* ✓ implemented */             \
  TRACE_SUBDOMAIN_END()                                                        \
                                                                               \
  /* [MPI(p2p)] — blocking point-to-point */                                   \
  TRACE_SUBDOMAIN_BEGIN("p2p")                                                 \
  TRACE_EVENT("MPI_Send",    1, 3, NULL)  /* ✓ implemented */                 \
  TRACE_EVENT("MPI_Recv",    1, 4, NULL)  /* ✓ implemented */                 \
  TRACE_EVENT("MPI_Sendrecv",1, 5, NULL)  /* ✓ implemented */                 \
  TRACE_SUBDOMAIN_END()                                                        \
                                                                               \
  /* [MPI(asyncp2p)] — non-blocking p2p; AMReX halo exchange pattern */       \
  TRACE_SUBDOMAIN_BEGIN("asyncp2p")                                            \
  TRACE_EVENT("MPI_Isend",   1,  6, NULL)  /* ✓ implemented */                \
  TRACE_EVENT("MPI_Irecv",   1,  7, NULL)  /* ✓ implemented */                \
  TRACE_EVENT("MPI_Wait",    1,  8, NULL)  /* ✓ implemented */                \
  TRACE_EVENT("MPI_Waitall", 1,  9, NULL)  /* ✓ implemented */                \
  TRACE_EVENT("MPI_Test",    0, 10, NULL)  /* - no wrapper yet */             \
  TRACE_SUBDOMAIN_END()                                                        \
                                                                               \
  /* [MPI(collective)] */                                                      \
  TRACE_SUBDOMAIN_BEGIN("collective")                                          \
  TRACE_EVENT("MPI_Bcast",     1, 11, NULL)  /* ✓ implemented */              \
  TRACE_EVENT("MPI_Barrier",   1, 12, NULL)  /* ✓ implemented */              \
  TRACE_EVENT("MPI_Reduce",    1, 13, NULL)  /* ✓ implemented */              \
  TRACE_EVENT("MPI_Allreduce", 1, 14, NULL)  /* ✓ implemented */              \
  TRACE_EVENT("MPI_Scatter",   1, 15, NULL)  /* ✓ implemented */              \
  TRACE_EVENT("MPI_Gather",    1, 16, NULL)  /* ✓ implemented */              \
  TRACE_EVENT("MPI_Allgather", 1, 17, NULL)  /* ✓ implemented */              \
  TRACE_EVENT("MPI_Alltoall",  1, 18, NULL)  /* ✓ implemented */              \
  TRACE_SUBDOMAIN_END()                                                        \
                                                                               \
  /* [MPI(others)] — communicator/type utilities; off by default */            \
  TRACE_SUBDOMAIN_BEGIN("others")                                              \
  TRACE_EVENT("MPI_Comm_rank",  0, 19, NULL)  /* - no wrapper yet */          \
  TRACE_EVENT("MPI_Comm_size",  0, 20, NULL)  /* - no wrapper yet */          \
  TRACE_EVENT("MPI_Comm_split", 0, 21, NULL)  /* - no wrapper yet */          \
  TRACE_EVENT("MPI_Comm_create",0, 22, NULL)  /* - no wrapper yet */          \
  TRACE_EVENT("MPI_Comm_dup",   0, 23, NULL)  /* - no wrapper yet */          \
                                                                               \
  TRACE_EVENT("MPI_Type_commit",0, 24, NULL)  /* - no wrapper yet */          \
  TRACE_EVENT("MPI_Type_free",  0, 25, NULL)  /* - no wrapper yet */          \
                                                                               \
  TRACE_EVENT("MPI_Wtime",  0, 26, NULL)  /* - no wrapper yet */              \
  TRACE_EVENT("MPI_Wtick",  0, 27, NULL)  /* - no wrapper yet */              \
  TRACE_SUBDOMAIN_END()                                                        \
                                                                               \
  /* [MPI(oneside)] — one-sided RMA; off by default */                         \
  TRACE_SUBDOMAIN_BEGIN("oneside")                                             \
  TRACE_EVENT("MPI_Win_create", 0, 28, NULL)  /* - no wrapper yet */          \
  TRACE_EVENT("MPI_Win_lock",   0, 29, NULL)  /* - no wrapper yet */          \
  TRACE_EVENT("MPI_Win_unlock", 0, 30, NULL)  /* - no wrapper yet */          \
  TRACE_EVENT("MPI_Put",        0, 31, NULL)  /* - no wrapper yet */          \
  TRACE_EVENT("MPI_Get",        0, 32, NULL)  /* - no wrapper yet */          \
  TRACE_EVENT("MPI_Accumulate", 0, 33, NULL)  /* - no wrapper yet */          \
  TRACE_SUBDOMAIN_END()                                                        \
                                                                               \
  /* [MPI(io)] — MPI-IO; off by default */                                     \
  TRACE_SUBDOMAIN_BEGIN("io")                                                  \
  TRACE_EVENT("MPI_File_open",  0, 34, NULL)  /* - no wrapper yet */          \
  TRACE_EVENT("MPI_File_close", 0, 35, NULL)  /* - no wrapper yet */          \
  TRACE_EVENT("MPI_File_read",  0, 36, NULL)  /* - no wrapper yet */          \
  TRACE_EVENT("MPI_File_write", 0, 37, NULL)  /* - no wrapper yet */          \
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
  MPI_domain_info->starting_mode = PINSIGHT_DOMAIN_TRACING;
  MPI_trace_config = &domain_default_trace_config[MPI_domain_index];
  return MPI_domain_info;
}

#endif /* MPI_DOMAIN_H */
