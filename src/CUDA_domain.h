#ifndef CUDA_DOMAIN_H
#define CUDA_DOMAIN_H

#include "trace_domain_dsl.h"
#include "trace_domain_loader.h"
#include "trace_config.h"

extern int CUDA_get_device_id(void*); //function to get the CUDA device id

/* Provided by your core implementation (e.g., trace_domain_loader.c) */
extern struct domain_info domain_info_table[];
extern int num_domain;
extern int CUDA_domain_index;
extern domain_info_t *CUDA_domain_info;
extern domain_trace_config_t *CUDA_trace_config;

/* --- 1. DSL BLOCK: CUDA domain definition (data only) --- */
#define CUDA_DOMAIN_DEFINITION                                      \
    TRACE_DOMAIN_BEGIN("CUDA", TRACE_EVENT_ID_INTERNAL)                                      \
                                                                    \
        /* [CUDA.device(0-16)] */                                   \
        TRACE_PUNIT1("device", 0, 16, CUDA_get_device_id, NULL)                                \
                                                                    \
        /* [CUDA(contextdevice)] */                                 \
        TRACE_SUBDOMAIN_BEGIN("contextdevice")                      \
            TRACE_EVENT( 0, "CUDA_context_create",     0)           \
            TRACE_EVENT( 1, "CUDA_context_destroy",    0)           \
                                                                    \
            TRACE_EVENT( 2, "CUDA_device_init",        0)           \
            TRACE_EVENT( 3, "CUDA_device_reset",       0)           \
            TRACE_EVENT( 4, "CUDA_device_synchronize", 0)           \
        TRACE_SUBDOMAIN_END()                                      \
                                                                    \
        /* [CUDA(streamevent)] */                                   \
        TRACE_SUBDOMAIN_BEGIN("streamevent")                        \
            TRACE_EVENT( 5, "CUDA_stream_create",       0)          \
            TRACE_EVENT( 6, "CUDA_stream_destroy",      0)          \
            TRACE_EVENT( 7, "CUDA_stream_synchronize",  0)          \
                                                                    \
            TRACE_EVENT( 8, "CUDA_event_create",        0)          \
            TRACE_EVENT( 9, "CUDA_event_record",        0)          \
            TRACE_EVENT(10, "CUDA_event_synchronize",   0)          \
            TRACE_EVENT(11, "CUDA_event_destroy",       0)          \
        TRACE_SUBDOMAIN_END()                                      \
                                                                    \
        /* [CUDA(memcpy)] */                                        \
        TRACE_SUBDOMAIN_BEGIN("memcpy")                             \
            TRACE_EVENT(12, "CUDA_memcpy_HtoD",         1)          \
            TRACE_EVENT(13, "CUDA_memcpy_DtoH",         1)          \
            TRACE_EVENT(14, "CUDA_memcpy_DtoD",         1)          \
            TRACE_EVENT(15, "CUDA_memcpy_HtoH",         1)          \
            TRACE_EVENT(16, "CUDA_memcpy_async",        1)          \
        TRACE_SUBDOMAIN_END()                                      \
                                                                    \
        /* [CUDA(malloc)] */                                        \
        TRACE_SUBDOMAIN_BEGIN("malloc")                             \
            TRACE_EVENT(17, "CUDA_memset",             0)           \
            TRACE_EVENT(18, "CUDA_malloc",             0)           \
            TRACE_EVENT(19, "CUDA_free",               0)           \
            TRACE_EVENT(20, "CUDA_malloc_host",        0)           \
            TRACE_EVENT(21, "CUDA_free_host",          0)           \
            TRACE_EVENT(22, "CUDA_malloc_managed",     0)           \
        TRACE_SUBDOMAIN_END()                                      \
                                                                    \
        /* [CUDA(kernel)] */                                        \
        TRACE_SUBDOMAIN_BEGIN("kernel")                             \
            TRACE_EVENT(23, "CUDA_kernel_launch",      1)           \
            TRACE_EVENT(24, "CUDA_kernel_complete",    0)           \
            TRACE_EVENT(25, "CUDA_kernel_enqueue",     0)           \
        TRACE_SUBDOMAIN_END()                                      \
                                                                    \
        /* [CUDA(others)] */                                        \
        TRACE_SUBDOMAIN_BEGIN("others")                             \
            TRACE_EVENT(26, "CUDA_api_enter",          0)           \
            TRACE_EVENT(27, "CUDA_api_exit",           0)           \
                                                                    \
            TRACE_EVENT(28, "CUDA_driver_call",        0)           \
            TRACE_EVENT(29, "CUDA_runtime_call",       0)           \
                                                                    \
            TRACE_EVENT(30, "CUDA_synchronize",        0)           \
            TRACE_EVENT(31, "CUDA_context_push",       0)           \
            TRACE_EVENT(32, "CUDA_context_pop",        0)           \
                                                                    \
            TRACE_EVENT(33, "CUDA_module_load",        0)           \
            TRACE_EVENT(34, "CUDA_module_unload",      0)           \
            TRACE_EVENT(35, "CUDA_function_load",      0)           \
                                                                    \
            TRACE_EVENT(36, "CUDA_graph_create",       0)           \
            TRACE_EVENT(37, "CUDA_graph_launch",       0)           \
            TRACE_EVENT(38, "CUDA_graph_destroy",      0)           \
                                                                    \
            TRACE_EVENT(39, "CUDA_unified_memory_migrate",  0)      \
            TRACE_EVENT(40, "CUDA_unified_memory_prefetch", 0)      \
                                                                    \
            TRACE_EVENT(41, "CUDA_nvtx_range_push",    0)           \
            TRACE_EVENT(42, "CUDA_nvtx_range_pop",     0)           \
            TRACE_EVENT(43, "CUDA_nvtx_mark",          0)           \
        TRACE_SUBDOMAIN_END()                                      \
                                                                    \
    TRACE_DOMAIN_END()

/* --- 2. Registration function (returns pointer to this CUDA domain) --- */

static inline struct domain_info *register_CUDA_trace_domain(void)
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

    /* Expand the CUDA definition into actual calls */
    CUDA_DOMAIN_DEFINITION;

    /* Cleanup macro namespace */
    #undef TRACE_IMPL_EVENT
    #undef TRACE_IMPL_SUBDOMAIN_END
    #undef TRACE_IMPL_SUBDOMAIN_BEGIN
    #undef TRACE_IMPL_PUNIT
    #undef TRACE_IMPL_DOMAIN_END
    #undef TRACE_IMPL_DOMAIN_BEGIN

    /* Return pointer to this domain’s entry */
    CUDA_domain_info = &domain_info_table[CUDA_domain_index];
    CUDA_trace_config = &domain_trace_config[CUDA_domain_index];
    return CUDA_domain_info;
}

#endif /* CUDA_DOMAIN_H */
