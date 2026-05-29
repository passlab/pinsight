#ifndef HIP_DOMAIN_H
#define HIP_DOMAIN_H

#include "trace_config.h"
#include "trace_domain_dsl.h"
#include "trace_domain_loader.h"

extern int HIP_get_device_id(void *);

extern struct domain_info domain_info_table[];
extern int num_domain;
extern int HIP_domain_index;
extern domain_info_t *HIP_domain_info;
extern domain_trace_config_t *HIP_trace_config;

/*
 * HIP domain definition.
 *
 * Events use dense internal IDs (TRACE_EVENT_ID_INTERNAL), numbered
 * sequentially within each subdomain in declaration order.  The constants
 * in roctracer_callback.c (HIP_EVENT_*) must match these offsets exactly.
 *
 * Implementation status:
 *   ✓ CB  = ROCTracer Callback API implemented in roctracer_callback.c
 *   ✓ ACT = ROCTracer Activity API record (GPU-side timestamps)
 *   -     = event declared but no callback registered yet
 *
 * Subdomain → dense ID range:
 *   device    :  0-5
 *   context   :  6-9
 *   stream    : 10-14
 *   eventmgmt : 15-19
 *   malloc    : 20-24
 *   memcpy    : 25-31
 *   kernel    : 32-34
 *   graph     : 35-38
 *   module    : 39-41
 *   others    : 42-51
 */

#define HIP_DOMAIN_DEFINITION                                                  \
  TRACE_DOMAIN_BEGIN("HIP", TRACE_EVENT_ID_INTERNAL)                          \
                                                                               \
  /* [HIP.device(0-16)] */                                                     \
  TRACE_PUNIT1("device", 0, 16, HIP_get_device_id, NULL)                      \
                                                                               \
  /* [HIP(device)] — IDs 0-5 */                                                \
  TRACE_SUBDOMAIN_BEGIN("device")                                              \
  TRACE_EVENT("HIP_device_reset",              0,  0, NULL) /* -          */  \
  TRACE_EVENT("HIP_device_synchronize",        1,  1, NULL) /* ✓ CB       */  \
  TRACE_EVENT("HIP_device_get",                0,  2, NULL) /* -          */  \
  TRACE_EVENT("HIP_device_set",                0,  3, NULL) /* -          */  \
  TRACE_EVENT("HIP_device_enable_peer_access", 0,  4, NULL) /* -          */  \
  TRACE_EVENT("HIP_device_disable_peer_access",0,  5, NULL) /* -          */  \
  TRACE_SUBDOMAIN_END()                                                        \
                                                                               \
  /* [HIP(context)] — IDs 6-9 */                                               \
  TRACE_SUBDOMAIN_BEGIN("context")                                             \
  TRACE_EVENT("HIP_context_create",  0,  6, NULL)  /* -                   */  \
  TRACE_EVENT("HIP_context_destroy", 0,  7, NULL)  /* -                   */  \
  TRACE_EVENT("HIP_context_push",    0,  8, NULL)  /* -                   */  \
  TRACE_EVENT("HIP_context_pop",     0,  9, NULL)  /* -                   */  \
  TRACE_SUBDOMAIN_END()                                                        \
                                                                               \
  /* [HIP(stream)] — IDs 10-14 */                                              \
  TRACE_SUBDOMAIN_BEGIN("stream")                                              \
  TRACE_EVENT("HIP_stream_create",       0, 10, NULL) /* -                */  \
  TRACE_EVENT("HIP_stream_destroy",      0, 11, NULL) /* -                */  \
  TRACE_EVENT("HIP_stream_synchronize",  1, 12, NULL) /* ✓ CB             */  \
  TRACE_EVENT("HIP_stream_wait_event",   0, 13, NULL) /* -                */  \
  TRACE_EVENT("HIP_stream_add_callback", 0, 14, NULL) /* -                */  \
  TRACE_SUBDOMAIN_END()                                                        \
                                                                               \
  /* [HIP(eventmgmt)] — IDs 15-19 */                                           \
  TRACE_SUBDOMAIN_BEGIN("eventmgmt")                                           \
  TRACE_EVENT("HIP_event_create",       0, 15, NULL) /* -                 */  \
  TRACE_EVENT("HIP_event_record",       0, 16, NULL) /* -                 */  \
  TRACE_EVENT("HIP_event_synchronize",  0, 17, NULL) /* -                 */  \
  TRACE_EVENT("HIP_event_destroy",      0, 18, NULL) /* -                 */  \
  TRACE_EVENT("HIP_event_elapsed_time", 0, 19, NULL) /* -                 */  \
  TRACE_SUBDOMAIN_END()                                                        \
                                                                               \
  /* [HIP(malloc)] — IDs 20-24 */                                              \
  TRACE_SUBDOMAIN_BEGIN("malloc")                                              \
  TRACE_EVENT("HIP_malloc",         0, 20, NULL) /* -                     */  \
  TRACE_EVENT("HIP_free",           0, 21, NULL) /* -                     */  \
  TRACE_EVENT("HIP_host_malloc",    0, 22, NULL) /* -   hipHostMalloc()   */  \
  TRACE_EVENT("HIP_host_free",      0, 23, NULL) /* -   hipHostFree()     */  \
  TRACE_EVENT("HIP_malloc_managed", 0, 24, NULL) /* -   hipMallocManaged()*/  \
  TRACE_SUBDOMAIN_END()                                                        \
                                                                               \
  /* [HIP(memcpy)] — IDs 25-31 */                                              \
  /* On MI300A (unified HBM), HtoD/DtoH are intra-HBM copies — near-zero  */  \
  /* duration in Activity records; keep enabled to measure actual behavior. */  \
  TRACE_SUBDOMAIN_BEGIN("memcpy")                                              \
  TRACE_EVENT("HIP_memcpy_HtoD",  1, 25, NULL) /* ✓ CB + ✓ ACT           */  \
  TRACE_EVENT("HIP_memcpy_DtoH",  1, 26, NULL) /* ✓ CB + ✓ ACT           */  \
  TRACE_EVENT("HIP_memcpy_DtoD",  1, 27, NULL) /* ✓ CB + ✓ ACT           */  \
  TRACE_EVENT("HIP_memcpy_HtoH",  1, 28, NULL) /* ✓ CB + ✓ ACT           */  \
  TRACE_EVENT("HIP_memcpy_async", 1, 29, NULL) /* ✓ CB + ✓ ACT           */  \
  TRACE_EVENT("HIP_memset",       0, 30, NULL) /* -                       */  \
  TRACE_EVENT("HIP_memset_async", 0, 31, NULL) /* -                       */  \
  TRACE_SUBDOMAIN_END()                                                        \
                                                                               \
  /* [HIP(kernel)] — IDs 32-34 */                                              \
  TRACE_SUBDOMAIN_BEGIN("kernel")                                              \
  TRACE_EVENT("HIP_kernel_launch",   1, 32, NULL) /* ✓ CB + ✓ ACT        */  \
  TRACE_EVENT("HIP_kernel_complete", 0, 33, NULL) /* - activity-only      */  \
  TRACE_EVENT("HIP_kernel_enqueue",  0, 34, NULL) /* - hipModuleLaunchKernel */\
  TRACE_SUBDOMAIN_END()                                                        \
                                                                               \
  /* [HIP(graph)] — IDs 35-38 */                                               \
  TRACE_SUBDOMAIN_BEGIN("graph")                                               \
  TRACE_EVENT("HIP_graph_create",    0, 35, NULL) /* -                    */  \
  TRACE_EVENT("HIP_graph_add_kernel",0, 36, NULL) /* - hipGraphAddKernelNode */\
  TRACE_EVENT("HIP_graph_launch",    0, 37, NULL) /* -                    */  \
  TRACE_EVENT("HIP_graph_destroy",   0, 38, NULL) /* -                    */  \
  TRACE_SUBDOMAIN_END()                                                        \
                                                                               \
  /* [HIP(module)] — IDs 39-41 */                                              \
  TRACE_SUBDOMAIN_BEGIN("module")                                              \
  TRACE_EVENT("HIP_module_load",         0, 39, NULL) /* -                */  \
  TRACE_EVENT("HIP_module_unload",       0, 40, NULL) /* -                */  \
  TRACE_EVENT("HIP_module_get_function", 0, 41, NULL) /* - hipModuleGetFunction */\
  TRACE_SUBDOMAIN_END()                                                        \
                                                                               \
  /* [HIP(others)] — IDs 42-51 */                                              \
  TRACE_SUBDOMAIN_BEGIN("others")                                              \
  TRACE_EVENT("HIP_init",                    0, 42, NULL) /* -            */  \
  TRACE_EVENT("HIP_api_enter",               0, 43, NULL) /* -            */  \
  TRACE_EVENT("HIP_api_exit",                0, 44, NULL) /* -            */  \
  TRACE_EVENT("HIP_driver_call",             0, 45, NULL) /* - HSA level  */  \
  TRACE_EVENT("HIP_runtime_call",            0, 46, NULL) /* -            */  \
  TRACE_EVENT("HIP_roctx_range_push",        0, 47, NULL) /* -            */  \
  TRACE_EVENT("HIP_roctx_range_pop",         0, 48, NULL) /* -            */  \
  TRACE_EVENT("HIP_roctx_mark",              0, 49, NULL) /* -            */  \
  TRACE_EVENT("HIP_unified_memory_prefetch", 0, 50, NULL) /* - hipMemPrefetchAsync */\
  TRACE_EVENT("HIP_unified_memory_advise",   0, 51, NULL) /* - hipMemAdvise      */\
  TRACE_SUBDOMAIN_END()                                                        \
                                                                               \
  TRACE_DOMAIN_END()


static inline struct domain_info *register_HIP_trace_domain(void) {
  HIP_domain_index = num_domain;

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

  HIP_DOMAIN_DEFINITION;

#undef TRACE_IMPL_EVENT
#undef TRACE_IMPL_SUBDOMAIN_END
#undef TRACE_IMPL_SUBDOMAIN_BEGIN
#undef TRACE_IMPL_PUNIT
#undef TRACE_IMPL_DOMAIN_END
#undef TRACE_IMPL_DOMAIN_BEGIN

  HIP_domain_info = &domain_info_table[HIP_domain_index];
  HIP_domain_info->starting_mode = PINSIGHT_DOMAIN_TRACING;
  HIP_trace_config = &domain_default_trace_config[HIP_domain_index];
  return HIP_domain_info;
}

#endif /* HIP_DOMAIN_H */
