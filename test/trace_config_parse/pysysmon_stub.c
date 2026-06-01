/* Stub providing Python domain symbols for test_config_parser.
 * The test exercises config parsing only; no Python C API needed. */
#include "trace_config.h"

int Python_domain_index;
domain_info_t *Python_domain_info;
domain_trace_config_t *Python_trace_config;
int pysysmon_get_thread_id(void) { return 0; }
