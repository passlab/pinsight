#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include "trace_config.h"

struct domain_info domain_info_table[MAX_NUM_DOMAINS];
int num_domain = 0;

/** The global default trace config */
trace_config_t trace_config[MAX_NUM_DOMAINS];

/**
 * This function is not thread safe, but it might not hurt called by multiple threads
 */
__attribute__ ((constructor)) void initial_lexgion_trace_config() {
    //lexgion_trace_config_sysdefault();
    //lexgion_trace_config_read_env_rtdefault();
    //lexgion_trace_config_read_file();
    //print_lexgion_trace_config();
}

long env_get_long(const char* varname, long default_value) {
    const char* str = getenv(varname);
    long out = default_value;
    // strtol segfaults if given a NULL ptr. Check before use!
    if (str != NULL) {
        out = strtol(str, NULL, 0);
    }
    // Error occurred in parsing, return default value.
    if (errno == EINVAL || errno == ERANGE) {
        out = default_value;
    }
    return out;
}

unsigned long env_get_ulong(const char* varname, unsigned long default_value) {
    const char* str = getenv(varname);
    unsigned long out = default_value;
    // strtoul segfaults if given a NULL ptr. Check before use!
    if (str != NULL) {
        out = strtoul(str, NULL, 0);
    }
    // Error occurred in parsing, return default value.
    if (errno == EINVAL || errno == ERANGE) {
        out = default_value;
    }
    return out;
}
