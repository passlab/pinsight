#ifndef MPI_DOMAIN_H
#define MPI_DOMAIN_H

extern int MPI_get_rank(void); //function to get the MPI rank


#include "trace_domain_dsl.h"
#include "trace_domain_loader.h"
#include "trace_config.h"

/* Provided by your core implementation (e.g. trace_domain_loader.c) */
extern struct domain_info domain_info_table[];
extern int num_domain;

/* --- 1. DSL BLOCK: MPI domain definition (data only) --- */

#define MPI_DOMAIN_DEFINITION                                   \
    TRACE_DOMAIN_BEGIN("MPI", TRACE_EVENT_ID_NATIVE)            \
                                                                \
        /* [MPI.rank(0-127)] */                                 \
        TRACE_PUNIT("rank", 0, 127, MPI_get_rank)                             \
                                                                \
        /* [MPI(init)] */                                       \
        TRACE_SUBDOMAIN_BEGIN("init")                           \
            TRACE_EVENT( 0, "MPI_Init",        0)               \
            TRACE_EVENT( 1, "MPI_Init_thread", 0)               \
            TRACE_EVENT( 2, "MPI_Finalize",    0)               \
        TRACE_SUBDOMAIN_END()                                   \
                                                                \
        /* [MPI(p2p)] */                                        \
        TRACE_SUBDOMAIN_BEGIN("p2p")                            \
            TRACE_EVENT( 3, "MPI_Send",      0)                 \
            TRACE_EVENT( 4, "MPI_Recv",      0)                 \
            TRACE_EVENT( 5, "MPI_Sendrecv",  0)                 \
        TRACE_SUBDOMAIN_END()                                   \
                                                                \
        /* [MPI(asyncp2p)] */                                   \
        TRACE_SUBDOMAIN_BEGIN("asyncp2p")                       \
            TRACE_EVENT( 6, "MPI_Isend",     0)                 \
            TRACE_EVENT( 7, "MPI_Irecv",     0)                 \
            TRACE_EVENT( 8, "MPI_Wait",      0)                 \
            TRACE_EVENT( 9, "MPI_Waitall",   0)                 \
            TRACE_EVENT(10, "MPI_Test",      0)                 \
        TRACE_SUBDOMAIN_END()                                   \
                                                                \
        /* [MPI(collective)] */                                 \
        TRACE_SUBDOMAIN_BEGIN("collective")                     \
            TRACE_EVENT(11, "MPI_Bcast",     0)                 \
            TRACE_EVENT(12, "MPI_Barrier",   0)                 \
            TRACE_EVENT(13, "MPI_Reduce",    0)                 \
            TRACE_EVENT(14, "MPI_Allreduce", 0)                 \
            TRACE_EVENT(15, "MPI_Scatter",   0)                 \
            TRACE_EVENT(16, "MPI_Gather",    0)                 \
            TRACE_EVENT(17, "MPI_Allgather", 0)                 \
            TRACE_EVENT(18, "MPI_Alltoall",  0)                 \
        TRACE_SUBDOMAIN_END()                                   \
                                                                \
        /* [MPI(others)] */                                     \
        TRACE_SUBDOMAIN_BEGIN("others")                         \
            TRACE_EVENT(19, "MPI_Comm_rank",   0)               \
            TRACE_EVENT(20, "MPI_Comm_size",   0)               \
            TRACE_EVENT(21, "MPI_Comm_split",  0)               \
            TRACE_EVENT(22, "MPI_Comm_create", 0)               \
            TRACE_EVENT(23, "MPI_Comm_dup",    0)               \
                                                                \
            TRACE_EVENT(24, "MPI_Type_commit", 0)               \
            TRACE_EVENT(25, "MPI_Type_free",   0)               \
                                                                \
            TRACE_EVENT(26, "MPI_Wtime",      0)                \
            TRACE_EVENT(27, "MPI_Wtick",      0)                \
        TRACE_SUBDOMAIN_END()                                   \
                                                                \
        /* [MPI(oneside)] */                                    \
        TRACE_SUBDOMAIN_BEGIN("oneside")                        \
            TRACE_EVENT(28, "MPI_Win_create", 0)                \
            TRACE_EVENT(29, "MPI_Win_lock",   0)                \
            TRACE_EVENT(30, "MPI_Win_unlock", 0)                \
            TRACE_EVENT(31, "MPI_Put",        0)                \
            TRACE_EVENT(32, "MPI_Get",        0)                \
            TRACE_EVENT(33, "MPI_Accumulate", 0)                \
        TRACE_SUBDOMAIN_END()                                   \
                                                                \
        /* [MPI(io)] */                                         \
        TRACE_SUBDOMAIN_BEGIN("io")                             \
            TRACE_EVENT(34, "MPI_File_open",  0)                \
            TRACE_EVENT(35, "MPI_File_close", 0)                \
            TRACE_EVENT(36, "MPI_File_read",  0)                \
            TRACE_EVENT(37, "MPI_File_write", 0)                \
        TRACE_SUBDOMAIN_END()                                   \
                                                                \
    TRACE_DOMAIN_END()

/* --- 2. Registration function (returns pointer to this MPI domain) --- */

static inline struct domain_info *register_mpi_domain(void)
{
    int domain_index_before = num_domain;

    /* Bind DSL macros to the generic helpers */

    #define TRACE_IMPL_DOMAIN_BEGIN(name, mode)   \
        do {                                \
            dsl_add_domain((name), (mode));

    #define TRACE_IMPL_DOMAIN_END()         \
        } while (0)

	#define TRACE_IMPL_PUNIT(name, low, high, punit_id_func, arg, num_arg) \
        dsl_add_punit((name), (low), (high), ((int(*)())(punit_id_func)), (arg), (num_arg));

    #define TRACE_IMPL_SUBDOMAIN_BEGIN(name) \
            { dsl_add_subdomain((name));

    #define TRACE_IMPL_SUBDOMAIN_END() \
            }

    #define TRACE_IMPL_EVENT(native_id, name, initial_status) \
                dsl_add_event((native_id), (name), (initial_status));

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
    return &domain_info_table[domain_index_before];
}

#endif /* MPI_DOMAIN_H */
