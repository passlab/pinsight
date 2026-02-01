#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include "trace_config.h"

struct domain_info domain_info_table[MAX_NUM_DOMAINS];
trace_config_t trace_config[MAX_NUM_DOMAINS];
int num_domain = 0;

/** The trace config */

/**
 * Check the trace config to see whether an event is set or not for tracing
 */
int trace_config_event_set(trace_config_t* dtcf, int event_id) {
	//check whether the domain is enabled
	if (!dtcf->set) {
		return 0;
	}
	//check whether the event is enabled in the domain default
	if (!bitset_test(&dtcf->event_config.status, (size_t)event_id)) {
		return 0;
	}
	//domain default is 1 at this point of execution

	punit_trace_config_t* punit_tcf = dtcf->punit_trace_config;
	if (punit_tcf == NULL) {
		return 1; //no punit specific trace config, return the domain default
	}

	while (punit_tcf != NULL) {//Check punit specific trace configs to find a match
		int i;
		int match = 1;
		for (i=0; i<num_domain; i++) {
			if (!punit_tcf->domain_punits[i].set) continue;
			//check whether the current execution punit id is in the punit id set
			struct domain_info* d = &domain_info_table[i];
			int k;
			for (k=0; k<d->num_punits; k++) {
				if (!punit_tcf->domain_punits[i].punit[k].set) {
					continue; //this punit kind is not constrained in this trace config
				}
				struct punit* p = &d->punits[k];
				int punit_id;
				if (p->num_arg == 0) {
					punit_id = p->punit_id_func.func0();
				} else {
					punit_id = p->punit_id_func.func1(p->arg);
				}
				if (punit_id < p->low || punit_id > p->high ||
					!bitset_test(&punit_tcf->domain_punits[i].punit[k].punit_ids, (size_t)punit_id)) {
					match = 0;
					break;
				}
			}
			if (!match) {
				break; //break the for loop and check the next trace config
			}
		}
		if (match) {
			return bitset_test(&punit_tcf->event_config.status, (size_t)event_id);
		}
		punit_tcf = punit_tcf->next;
	}
	return 0;
}
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
