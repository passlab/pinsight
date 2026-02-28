#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include "pinsight_config.h"
#include "trace_config.h"
#include "trace_config_parse.h"
#ifdef PINSIGHT_MPI
#include "trace_domain_MPI.h"
#endif

#ifdef PINSIGHT_OPENMP
#include "trace_domain_OpenMP.h"
#endif

#ifdef PINSIGHT_CUDA
#include <dlfcn.h>
#include "trace_domain_CUDA.h"
#endif

struct domain_info domain_info_table[MAX_NUM_DOMAINS];
domain_trace_config_t domain_default_trace_config[MAX_NUM_DOMAINS];
punit_trace_config_t  *domain_punit_trace_config[MAX_NUM_DOMAINS];
int num_domain = 0;

lexgion_trace_config_t all_lexgion_trace_config[MAX_NUM_DOMAINS + MAX_NUM_LEXGIONS + 1];
lexgion_trace_config_t *lexgion_default_trace_config = &all_lexgion_trace_config[0];
lexgion_trace_config_t *lexgion_domain_default_trace_config = &all_lexgion_trace_config[1];
lexgion_trace_config_t *lexgion_address_trace_config = &all_lexgion_trace_config[1+MAX_NUM_DOMAINS];
int num_lexgion_address_trace_configs = 0; 

#ifdef PINSIGHT_CUDA
static inline int pinsight_cuda_runtime_available(void);
#endif

/**
 * Check whether the current execution punit id's are in the punit id set or not of the domain_punit_set 
 * @param domain_punit_set the pointer to the domain_punit_set of all domains (not just a single domain)
 * @return 1 if the current execution punit id's are in the punit id set or not of the domain_punit_set, 0 otherwise
 */
int domain_punit_set_match(domain_punit_set_t* domain_punit_set) {
    int i;
    for (i=0; i<num_domain; i++) {
        if (!domain_punit_set->set) continue;
	    //check whether the current execution punit id is in the punit id set
	    struct domain_info* d = &domain_info_table[i];
        domain_punit_set_t * dpst = &domain_punit_set[i];
	    int k;
	    for (k=0; k<d->num_punits; k++) {
		    if (!dpst->punit[k].set) continue; //this punit kind is not constrained in this trace config
		    int punit_id;
		    if (d->punits[k].num_arg == 0) {
			    punit_id = d->punits[k].punit_id_func.func0();
		    } else {
			    punit_id = d->punits[k].punit_id_func.func1(d->punits[k].arg);
		    }
		    if (punit_id < d->punits[k].low || punit_id > d->punits[k].high ||
			    !bitset_test(&dpst->punit[k].punit_ids, (size_t)punit_id)) {
			    return 0;
		    }
	    }
    }
	return 1;
}

/**
 * Given a codeptr, lookup or reserve a config struct object
 * @param codeptr the pointer to the codeptr
 * @return the pointer to the config struct object
 */
lexgion_trace_config_t * retrieve_lexgion_trace_config(const void * codeptr) {
    int i;
    lexgion_trace_config_t  * config = NULL;
    for (i = 1; i < MAX_NUM_LEXGIONS; i++) {
        config = &lexgion_address_trace_config[i];
        if (config->codeptr == codeptr) {
            return config;
        }
    }
    return NULL;
}

void setup_trace_config_env() {
    // 1. Override Domain Defaults
    for (int i = 0; i < num_domain; i++) {
        char env_var[256];
        struct domain_info *d = &domain_info_table[i];
        
        // Construct PINSIGHT_TRACE_<DOMAIN>
        snprintf(env_var, sizeof(env_var), "PINSIGHT_TRACE_%s", d->name);
        // Convert to uppercase
        for(int j=0; env_var[j]; j++) env_var[j] = toupper((unsigned char)env_var[j]);

        char *val = getenv(env_var);
        if (val) {
            int enable = (strcasecmp(val, "TRUE") == 0 || strcmp(val, "1") == 0);
            domain_default_trace_config[i].set = enable;
        }
    }

    // 2. Override Lexgion Rate
    // PINSIGHT_TRACE_RATE=trace_starts_at:max_num_traces:tracing_rate
    char *rate_env = getenv("PINSIGHT_TRACE_RATE");
    if (rate_env) {
        int start = 0, max = 0, rate = 0;
        int count = sscanf(rate_env, "%d:%d:%d", &start, &max, &rate);
        if (count >= 1) lexgion_default_trace_config->trace_starts_at = start;
        if (count >= 2) lexgion_default_trace_config->max_num_traces = max;
        if (count >= 3) lexgion_default_trace_config->tracing_rate = rate;
    }
}

void pinsight_load_trace_config(char * filepath) {
    if (!filepath) {
        filepath = getenv("PINSIGHT_TRACE_CONFIG_FILE");
    }

    if (filepath) {
        parse_trace_config_file(filepath);
    } 
    setup_trace_config_env(); // Re-apply env overrides
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
    if (pinsight_cuda_runtime_available()) {
        register_CUDA_trace_domain();
    }
#endif
	// Initialize the default domain trace configs by copying from domain_info_table that has the installed events
	int i;
	for (i = 0; i < num_domain; i++) {
		domain_default_trace_config[i].events = domain_info_table[i].eventInstallStatus;
		if (domain_default_trace_config[i].events) {
			domain_default_trace_config[i].set = 1;
		} else {
			domain_default_trace_config[i].set = 0;
		}
	}

	// Initialize the default lexgion trace config
	lexgion_default_trace_config->codeptr = NULL;
	lexgion_default_trace_config->tracing_rate = 1; //trace every execution
	lexgion_default_trace_config->trace_starts_at = 0; //start tracing from the first execution	
	lexgion_default_trace_config->max_num_traces = -1; //unlimited traces

    pinsight_load_trace_config(NULL);
    print_domain_trace_config(stdout);
    print_lexgion_trace_config(stdout);
}

#ifdef PINSIGHT_CUDA
static inline int pinsight_cuda_runtime_available(void) {
    static int cached = -1;
    if (cached != -1) {
        return cached;
    }
    void *handle = dlopen("libcuda.so.1", RTLD_LAZY | RTLD_LOCAL);
    if (!handle) {
        handle = dlopen("libcuda.so", RTLD_LAZY | RTLD_LOCAL);
    }
    if (!handle) {
        fprintf(stderr, "[PInsight WARNING] CUDA support was compiled in, but libcuda.so is not available on this system. CUDA tracing will be disabled.\n");
        cached = 0;
        return 0;
    }
    dlclose(handle);
    cached = 1;
    return 1;
}
#endif


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
