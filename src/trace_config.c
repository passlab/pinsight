#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include "pinsight_config.h"
#include "trace_config.h"
#include "trace_config_parse.h"
#ifdef PINSIGHT_MPI
#include "MPI_domain.h"
#endif

#ifdef PINSIGHT_OPENMP
#include "OpenMP_domain.h"
#endif

#ifdef PINSIGHT_CUDA
#include "CUDA_domain.h"
#endif

struct domain_info domain_info_table[MAX_NUM_DOMAINS];
domain_trace_config_t domain_trace_config[MAX_NUM_DOMAINS];
int num_domain = 0;

lexgion_trace_config_t lexgion_trace_config[MAX_NUM_LEXGIONS];
int num_lexgion_trace_configs = 0;

/**
 * Check the trace config to see whether an event is set or not for tracing
 */
int trace_config_event_set(domain_trace_config_t* dtcf, int event_id) {
	//check whether the domain is enabled
	if (!dtcf->set) {
		return 0;
	}
	//check whether the event is enabled in the domain default
	if (!((dtcf->events >> event_id) & 1)) {
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
			return ((punit_tcf->events >> event_id) & 1);
		}
		punit_tcf = punit_tcf->next;
	}
	return 0;
}

/**
 * Given a codeptr, lookup or reserve a config struct object
 * 
 */
lexgion_trace_config_t * retrieve_lexgion_trace_config(const void * codeptr) {
	#if 0
    int i;
    int done = 0;
    lexgion_trace_config_t  * config = NULL;
    while (!done) {
        int unused_entry = -1;
        for (i = 0; i < MAX_NUM_LEXGIONS; i++) {
            config = &lexgion_trace_config[i];
            if (config->codeptr == codeptr) {/* one already exists, and we donot check class and type */
                /* trace_config already set before or by others, we just need to link */
                done = 1; /* to break the outer while loop */
                //printf("Found the config for lexgion %p: %p, %d by thread %d\n", codeptr, config, i, global_thread_num);
                break;
            } else if (config->codeptr == NULL && unused_entry == -1) {
                unused_entry = i; //find the first unused entry
            }
        }
        if (i == MAX_NUM_LEXGIONS) { //not find the one for the codeptr, allocate one
            config = &lexgion_trace_config[unused_entry]; /* we must have an unused entry */
            /* data race here, we must protect updating codeptr by multiple threads, use cas to do it */
            if (config->codeptr == NULL && __sync_bool_compare_and_swap((uint64_t*)&config->codeptr, NULL, codeptr)) {
                i = unused_entry;
                //printf("Allocate a config for lexgion %p: %p, %d by thread %d\n", config->codeptr, config, i, global_thread_num);
                break;
            } /* else, go back to the loop and check again */
        }
        if (unused_entry == MAX_NUM_LEXGIONS) {//assertation check
            //we have enough entries (MAX_NUM_LEXGIONS) for the max possible number of lexgions in the app.
            //otherwise, this is fault.

        }
    }
    return config;
	#endif
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

void setup_trace_config() {
	int i;
	for (i = 0; i < num_domain; i++) {
		domain_trace_config[i].events = domain_info_table[i].eventInstallStatus;
		domain_trace_config[i].punit_trace_config = NULL;
		if (domain_trace_config[i].events) {
			domain_trace_config[i].set = 1;
		} else {
			domain_trace_config[i].set = 0;
		}
	}
    char *env_file = getenv("PINSIGHT_TRACE_CONFIG_FILE");
    if (env_file) {
        parse_trace_config_file(env_file);
    } else {
        parse_trace_config_file("pinsight_trace_config.txt");
    }
}

__attribute__ ((constructor)) void initial_setup_trace_config() {
#ifdef PINSIGHT_OPENMP
    register_OpenMP_trace_domain();
    //OpenMP support is initialized by ompt_start_tool() callback that is implemented in ompt_callback.c, thus we do not need to initialize here.
#endif
#ifdef PINSIGHT_MPI
    register_MPI_trace_domain();
#endif
#ifdef PINSIGHT_CUDA
    //TODO: Also need runtime check
    register_CUDA_trace_domain();
#endif	
	setup_trace_config();
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
