/*
 * pysysmon_globals.c
 *
 * Domain globals and thread-ID assignment for the Python tracing domain.
 * Compiled into libpinsight.so so they are available when the constructor
 * calls register_Python_trace_domain() — before _pinsight_python.so is
 * imported by Python.
 *
 * pysysmon_callback.c (the Python C extension) calls pysysmon_get_thread_id()
 * via the libpinsight.so symbol, keeping the Python C API out of this file.
 */

#include "trace_config.h"

/* Domain registration globals — declared extern in trace_domain_Python.h */
int Python_domain_index;
domain_info_t *Python_domain_info;
domain_trace_config_t *Python_trace_config;

/* Thread-local sequential ID used by the punit system for config filtering.
 * 0-based within the Python domain (analogous to omp_get_thread_num()).
 * Non-static: accessed directly by pysysmon_callback.c for tracepoint fields. */
_Atomic int pysysmon_thread_counter = 0;
__thread int pysysmon_thread_id = -1;

int pysysmon_get_thread_id(void) {
  if (__builtin_expect(pysysmon_thread_id < 0, 0))
    pysysmon_thread_id =
        __atomic_fetch_add(&pysysmon_thread_counter, 1, __ATOMIC_RELAXED);
  return pysysmon_thread_id;
}
