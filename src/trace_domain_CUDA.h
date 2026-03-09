#ifndef CUDA_DOMAIN_H
#define CUDA_DOMAIN_H

#include "trace_config.h"
#include "trace_domain_dsl.h"
#include "trace_domain_loader.h"

extern int CUDA_get_device_id(void *); // function to get the CUDA device id

/* Provided by your core implementation (e.g., trace_domain_loader.c) */
extern struct domain_info domain_info_table[];
extern int num_domain;
extern int CUDA_domain_index;
extern domain_info_t *CUDA_domain_info;
extern domain_trace_config_t *CUDA_trace_config;

/* --- 1. DSL BLOCK: CUDA domain definition (data only) --- */
#define CUDA_DOMAIN_DEFINITION                                                 \
  TRACE_DOMAIN_BEGIN("CUDA", TRACE_EVENT_ID_INTERNAL)                          \
                                                                               \
  /* [CUDA.device(0-16)] */                                                    \
  TRACE_PUNIT1("device", 0, 16, CUDA_get_device_id, NULL)                      \
                                                                               \
  /* [CUDA(contextdevice)] */                                                  \
  TRACE_SUBDOMAIN_BEGIN("contextdevice")                                       \
  TRACE_EVENT("CUDA_context_create", 0, 0, NULL)                               \
  TRACE_EVENT("CUDA_context_destroy", 0, 1, NULL)                              \
                                                                               \
  TRACE_EVENT("CUDA_device_init", 0, 2, NULL)                                  \
  TRACE_EVENT("CUDA_device_reset", 0, 3, NULL)                                 \
  TRACE_EVENT("CUDA_device_synchronize", 0, 4, NULL)                           \
  TRACE_SUBDOMAIN_END()                                                        \
                                                                               \
  /* [CUDA(streamevent)] */                                                    \
  TRACE_SUBDOMAIN_BEGIN("streamevent")                                         \
  TRACE_EVENT("CUDA_stream_create", 0, 5, NULL)                                \
  TRACE_EVENT("CUDA_stream_destroy", 0, 6, NULL)                               \
  TRACE_EVENT("CUDA_stream_synchronize", 0, 7, NULL)                           \
                                                                               \
  TRACE_EVENT("CUDA_event_create", 0, 8, NULL)                                 \
  TRACE_EVENT("CUDA_event_record", 0, 9, NULL)                                 \
  TRACE_EVENT("CUDA_event_synchronize", 0, 10, NULL)                           \
  TRACE_EVENT("CUDA_event_destroy", 0, 11, NULL)                               \
  TRACE_SUBDOMAIN_END()                                                        \
                                                                               \
  /* [CUDA(memcpy)] */                                                         \
  TRACE_SUBDOMAIN_BEGIN("memcpy")                                              \
  TRACE_EVENT("CUDA_memcpy_HtoD", 1, 12, NULL)                                 \
  TRACE_EVENT("CUDA_memcpy_DtoH", 1, 13, NULL)                                 \
  TRACE_EVENT("CUDA_memcpy_DtoD", 1, 14, NULL)                                 \
  TRACE_EVENT("CUDA_memcpy_HtoH", 1, 15, NULL)                                 \
  TRACE_EVENT("CUDA_memcpy_async", 1, 16, NULL)                                \
  TRACE_SUBDOMAIN_END()                                                        \
                                                                               \
  /* [CUDA(malloc)] */                                                         \
  TRACE_SUBDOMAIN_BEGIN("malloc")                                              \
  TRACE_EVENT("CUDA_memset", 0, 17, NULL)                                      \
  TRACE_EVENT("CUDA_malloc", 0, 18, NULL)                                      \
  TRACE_EVENT("CUDA_free", 0, 19, NULL)                                        \
  TRACE_EVENT("CUDA_malloc_host", 0, 20, NULL)                                 \
  TRACE_EVENT("CUDA_free_host", 0, 21, NULL)                                   \
  TRACE_EVENT("CUDA_malloc_managed", 0, 22, NULL)                              \
  TRACE_SUBDOMAIN_END()                                                        \
                                                                               \
  /* [CUDA(kernel)] */                                                         \
  TRACE_SUBDOMAIN_BEGIN("kernel")                                              \
  TRACE_EVENT("CUDA_kernel_launch", 1, 23, NULL)                               \
  TRACE_EVENT("CUDA_kernel_complete", 0, 24, NULL)                             \
  TRACE_EVENT("CUDA_kernel_enqueue", 0, 25, NULL)                              \
  TRACE_SUBDOMAIN_END()                                                        \
                                                                               \
  /* [CUDA(others)] */                                                         \
  TRACE_SUBDOMAIN_BEGIN("others")                                              \
  TRACE_EVENT("CUDA_api_enter", 0, 26, NULL)                                   \
  TRACE_EVENT("CUDA_api_exit", 0, 27, NULL)                                    \
                                                                               \
  TRACE_EVENT("CUDA_driver_call", 0, 28, NULL)                                 \
  TRACE_EVENT("CUDA_runtime_call", 0, 29, NULL)                                \
                                                                               \
  TRACE_EVENT("CUDA_synchronize", 0, 30, NULL)                                 \
  TRACE_EVENT("CUDA_context_push", 0, 31, NULL)                                \
  TRACE_EVENT("CUDA_context_pop", 0, 32, NULL)                                 \
                                                                               \
  TRACE_EVENT("CUDA_module_load", 0, 33, NULL)                                 \
  TRACE_EVENT("CUDA_module_unload", 0, 34, NULL)                               \
  TRACE_EVENT("CUDA_function_load", 0, 35, NULL)                               \
                                                                               \
  TRACE_EVENT("CUDA_graph_create", 0, 36, NULL)                                \
  TRACE_EVENT("CUDA_graph_launch", 0, 37, NULL)                                \
  TRACE_EVENT("CUDA_graph_destroy", 0, 38, NULL)                               \
                                                                               \
  TRACE_EVENT("CUDA_unified_memory_migrate", 0, 39, NULL)                      \
  TRACE_EVENT("CUDA_unified_memory_prefetch", 0, 40, NULL)                     \
                                                                               \
  TRACE_EVENT("CUDA_nvtx_range_push", 0, 41, NULL)                             \
  TRACE_EVENT("CUDA_nvtx_range_pop", 0, 42, NULL)                              \
  TRACE_EVENT("CUDA_nvtx_mark", 0, 43, NULL)                                   \
  TRACE_SUBDOMAIN_END()                                                        \
                                                                               \
  TRACE_DOMAIN_END()

/* --- 2. Registration function (returns pointer to this CUDA domain) --- */

static inline struct domain_info *register_CUDA_trace_domain(void) {
  CUDA_domain_index =
      num_domain; /* assign to global, declared in cupti_callback.c */

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
  CUDA_trace_config = &domain_default_trace_config[CUDA_domain_index];
  return CUDA_domain_info;
}

#endif /* CUDA_DOMAIN_H */
