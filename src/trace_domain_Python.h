/* trace_domain_Python.h
 *
 * Python tracing domain registration via sys.monitoring (PEP 669).
 * Follows the same DSL pattern as trace_domain_OpenMP.h and trace_domain_CUDA.h.
 */
#ifndef PYTHON_DOMAIN_H
#define PYTHON_DOMAIN_H

#include "trace_config.h"
#include "trace_domain_dsl.h"
#include "trace_domain_loader.h"

/* Provided by pysysmon_callback.c */
extern int pysysmon_get_thread_id(void);

/* Provided by core implementation */
extern struct domain_info domain_info_table[];
extern int num_domain;
extern int Python_domain_index;
extern domain_info_t *Python_domain_info;
extern domain_trace_config_t *Python_trace_config;

/* --- 1. DSL BLOCK: Python domain definition (data only) ---
 *
 * Events (dense IDs, 0-based):
 *   0  pysysmon_py_start   — PY_START callback        (on by default)
 *   1  pysysmon_py_return  — PY_RETURN callback        (on by default)
 *   2  pysysmon_c_start    — CALL callback (C ext)     (on by default)
 *   3  pysysmon_c_return   — C_RETURN callback (C ext) (on by default)
 *   4  pysysmon_import     — reserved for future use   (off by default)
 *
 * Punits:
 *   thread(0-255)  — sequential Python thread ID via pysysmon_get_thread_id()
 *
 * Subdomains:
 *   function — Python function entry/exit
 *   bridge   — C extension call/return
 */

#define PYTHON_DOMAIN_DEFINITION                                               \
  TRACE_DOMAIN_BEGIN("Python", TRACE_EVENT_ID_INTERNAL)                        \
                                                                               \
  /* [Python.thread(0-255)] */                                                 \
  TRACE_PUNIT("thread", 0, 255, pysysmon_get_thread_id)                        \
                                                                               \
  /* [Python(function)] */                                                     \
  TRACE_SUBDOMAIN_BEGIN("function")                                            \
  TRACE_EVENT("pysysmon_py_start",  1, 0, NULL)                                \
  TRACE_EVENT("pysysmon_py_return", 1, 1, NULL)                                \
  TRACE_SUBDOMAIN_END()                                                        \
                                                                               \
  /* [Python(bridge)] */                                                       \
  TRACE_SUBDOMAIN_BEGIN("bridge")                                              \
  TRACE_EVENT("pysysmon_c_start",  1, 2, NULL)                                 \
  TRACE_EVENT("pysysmon_c_return", 1, 3, NULL)                                 \
  TRACE_SUBDOMAIN_END()                                                        \
                                                                               \
  /* [Python(others)] */                                                       \
  TRACE_SUBDOMAIN_BEGIN("others")                                              \
  TRACE_EVENT("pysysmon_import",   0, 4, NULL)                                 \
  TRACE_SUBDOMAIN_END()                                                        \
                                                                               \
  TRACE_DOMAIN_END()

/* --- 2. Registration function (returns pointer to this Python domain) --- */

static inline struct domain_info *register_Python_trace_domain(void) {
  Python_domain_index =
      num_domain; /* assign to global, declared in pysysmon_callback.c */

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

  /* Expand the Python definition into actual calls */
  PYTHON_DOMAIN_DEFINITION;

/* Cleanup macro namespace */
#undef TRACE_IMPL_EVENT
#undef TRACE_IMPL_SUBDOMAIN_END
#undef TRACE_IMPL_SUBDOMAIN_BEGIN
#undef TRACE_IMPL_PUNIT
#undef TRACE_IMPL_DOMAIN_END
#undef TRACE_IMPL_DOMAIN_BEGIN

  /* Return pointer to this domain's entry */
  Python_domain_info = &domain_info_table[Python_domain_index];
  Python_domain_info->starting_mode = PINSIGHT_DOMAIN_TRACING;
  Python_trace_config = &domain_default_trace_config[Python_domain_index];
  return Python_domain_info;
}

#endif /* PYTHON_DOMAIN_H */
