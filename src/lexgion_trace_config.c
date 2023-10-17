// PInsight environment variable querying functions.
// Copyright (c) 2018, PASSLab Team. All rights reserved.
#include <stdio.h>
#include <string.h>
#include "pinsight.h"

lexgion_trace_config_t trace_configs[MAX_NUM_LEXGIONS+2];
lexgion_trace_config_t *sysdefault_trace_config; //trace_configs[0]    //system default config, defined at built time
lexgion_trace_config_t *rtdefault_trace_config;  //trace_configs[1]    //runtime default config, configured at runtime
lexgion_trace_config_t *lexgion_trace_config;    //trace_configs[2...] //lexgion-specific config, configured at runtime

/**
 * Read the system default. This is set according to what features are enabled when building the library, check CMakeLists.txt file
 */
static void lexgion_trace_config_sysdefault() {
	sysdefault_trace_config = &trace_configs[0];
	rtdefault_trace_config = &trace_configs[1];
	lexgion_trace_config = &trace_configs[2];
    //set the default for the global config, which is full trace
#ifdef PINSIGHT_OPENMP
	sysdefault_trace_config[0].OpenMP_trace_enabled = 1;
#else
	sysdefault_trace_config[0].OpenMP_trace_enabled = 0;
#endif

#ifdef PINSIGHT_MPI
	sysdefault_trace_config[0].MPI_trace_enabled = 1;
#else
	sysdefault_trace_config[0].MPI_trace_enabled = 0;
#endif

#ifdef PINSIGHT_CUDA
	sysdefault_trace_config[0].CUDA_trace_enabled = 1;
#else
	sysdefault_trace_config[0].CUDA_trace_enabled = 0;
#endif

#ifdef PINSIGHT_ENERGY
	sysdefault_trace_config[0].ENERGY_trace_enabled = 1;
#else
	sysdefault_trace_config[0].ENERGY_trace_enabled = 0;
#endif

#ifdef PINSIGHT_BACKTRACE
	sysdefault_trace_config[0].BACKTRACE_enabled = 1;
#else
	sysdefault_trace_config[0].BACKTRACE_enabled = 0;
#endif
	sysdefault_trace_config[0].trace_starts_at = 0;     //system default
	sysdefault_trace_config[0].initial_trace_count = 0; //system default
	sysdefault_trace_config[0].max_num_traces = 10;     //system default
	sysdefault_trace_config[0].tracing_rate = 1;        //system default
	sysdefault_trace_config[0].updated = 0;
	sysdefault_trace_config[0].codeptr = NULL;

    //Initialize the default runtime trace config to be the same as the system default
    memcpy(rtdefault_trace_config, sysdefault_trace_config, sizeof(lexgion_trace_config_t));
}

/**
 * This function is not thread safe, but it seems won't hurt calling by multiple threads
 */
__attribute__ ((constructor)) void initial_lexgion_trace_config() {
    lexgion_trace_config_sysdefault();
    lexgion_trace_config_read_env();
    lexgion_trace_config_read_file();
    print_lexgion_trace_config();
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


/**
 * Given a codeptr, lookup or reserve a config struct object
 * 
 */
lexgion_trace_config_t * retrieve_lexgion_config(const void * codeptr) {
    int i;
    int done = 0;
    lexgion_trace_config_t  * config = NULL;
    while (!done) {
        int unused_entry = -1;
        for (i = 1; i < MAX_NUM_LEXGIONS; i++) {
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
}

/**
 * The function check whether the requested setting can be enabled by the system, i.e. a feature can only be enabled if
 * it is enabled by the system default.
 *
 * The function returns the setting: if request to enable, but system does not have this feature, return disabled. Otherwise,
 * return what is requested
 *
 * We can simply set it as system default, but having the checking in order to notify users
 */
static int feature_enabling_check(int value, int key_index) { //If the config wants to enable this feature
    if (((int*)sysdefault_trace_config)[key_index] == 0) {//but the sysdefault is not enabled, i.e. this feature is not available
	    value = 0; //disable this feature and notify users that feature is not enabled even it is requested.
        printf("PInsight WARNING: The feature \"%s\" is not supported by the library, thus cannot be enabled! Rebuild the library with support for it.\n", lexgion_trace_config_keys[key_index]);
    }
	return value;
}

/**
 * read in a config line, which is in the format of "key = value" into the config object. key must be a
 * valid identifier and value can only be "an integer, TRUE or FALSE"
 * @param config
 * @param lineBuffer
 *
 * @return error 1 failure, 0 success
 */
static int read_config_line(lexgion_trace_config_t * config, char * lineBuffer) {
    char key[64];
    char value[64];
    /* a simple parser to split and grab the key and value */
    sscanf(lineBuffer, "%s = %s", key, value);

    int i;
    for (i=0; i<NUM_CONFIG_KEYS; i++) {
        if (strcasecmp(key, lexgion_trace_config_keys[i]) == 0) {
            int intvalue;
            if (strcasecmp(value, "TRUE") == 0) {
                intvalue = 1;
            } else if (strcasecmp(value, "FALSE") == 0) {
                intvalue = 0;
            } else {
                intvalue = atoi(value);
            }
            //Check whether this feature is enabled by the sysdefault_trace_config. If so, we can enable here.
            //If not, this feature cannot be used. We can simply set it as system default, but doing so to notify users
            if (i < NUM_CONFIG_FEATURES && intvalue >= 1) intvalue = feature_enabling_check(intvalue, i);
            ((int*)config)[i] = intvalue;
            //printf("%s=%d\n", key, ((unsigned int*)config)[i]);
            return 0;
        }
    }
    return 1;
}

/**
 * Return the env and update the rtdefault_trace_config
 */
static void lexgion_trace_config_read_env_true_false(const char * env, int key_index) {
    const char* value = getenv(env);
    if (value == NULL) return;

    int intvalue;
    if (strcasecmp(value, "TRUE") == 0) {
        intvalue = 1;
        intvalue = feature_enabling_check(intvalue, key_index);
        ((int*)rtdefault_trace_config)[key_index] = intvalue;
    } else if (strcasecmp(value, "FALSE") == 0) {
        intvalue = 0;
        ((int*)rtdefault_trace_config)[key_index] = intvalue;
    } else { //wrong value, report back
        printf("PInsight WARNING: Incorrect setting \"%s\" for env %s, ignored!. It can only be set as TRUE or FALSE.\n", value, env);
    }
}

/**
 * Return the env and update the rtdefault_trace_config
 */
void lexgion_trace_config_read_env() {
	lexgion_trace_config_read_env_true_false("PINSIGHT_TRACE_OPENMP", OpenMP_trace_enabled_index);
	lexgion_trace_config_read_env_true_false("PINSIGHT_TRACE_MPI", MPI_trace_enabled_index);
	lexgion_trace_config_read_env_true_false("PINSIGHT_TRACE_CUDA", CUDA_trace_enabled_index);
	lexgion_trace_config_read_env_true_false("PINSIGHT_TRACE_ENERGY", ENERGY_trace_enabled_index);
	lexgion_trace_config_read_env_true_false("PINSIGHT_TRACE_BACKTRACE", BACKTRACE_enabled_index);

    const char* value = getenv("PINSIGHT_TRACE_RATE");
    if (value == NULL) return;
    /* The simpliest way to split the int values for
     * PINSIGHT_TRACE_RATE=<trace_starts_at>:<initial_trace_count>:<max_num_traces>:<tracing_rate>
     */
    sscanf(value, "%d:%d:%d:%d",
        (int*)rtdefault_trace_config + trace_starts_at_index,
        (int*)rtdefault_trace_config + initial_trace_count_index,
        (int*)rtdefault_trace_config + max_num_traces_index,
        (int*)rtdefault_trace_config + tracing_rate_index);
}

/**
 * Read the config file and store the configuration into a lexgion_trace_config_t object
 */
void lexgion_trace_config_read_file() {
    /* read in config from config file if it is provided via lexgion_trace_configFILE env */
    const char* configFileName = getenv("PINSIGHT_TRACE_CONFIG");
    FILE *configFile;
    if (configFileName != NULL) {
        configFile = fopen(configFileName, "r");
        if (configFile == NULL) {
            fprintf(stderr, "Cannot open config file %s, ignore. \n", configFileName);
            return;
        }
    } else return;

    //This is the default for fresh new config such that the config provided by the configFile will overwrite existing configuration
    //if this is false, it is incremental config

    /* Read the config file and store the configuration into a lexgion_trace_config_t object */
    lexgion_trace_config_t *config; /* The very first one should be initialized from the system */
    char lineBuffer[512];
    int max_char = 511;
    void * lexgion_codeptr;
    int line = 0;
    while (fgets(lineBuffer, max_char, configFile) != NULL) {
        char * ptr = lineBuffer;
        line++;
        /* ignore comment line which starts with #, and blank line. */
        while(*ptr==' ' || *ptr=='\t') ptr++; // skip leading whitespaces
        if (*ptr == '#' || *ptr=='\r' || *ptr=='\n') {//comment line or empty line
            continue;
        }

        if (*ptr == '[') {
            ptr++;
            while(*ptr==' ' || *ptr=='\t') ptr++; // skip leading whitespaces
            if (*ptr == 'd' && *++ptr == 'e' && *++ptr == 'f' && *++ptr == 'a' && *++ptr == 'u'
                && *++ptr == 'l' && *++ptr == 't') { //parse [default]
                ptr++;
                while(*ptr==' ' || *ptr=='\t') ptr++; // skip leading whitespaces
                if (*ptr == ']') {
                    ptr++;
                    while(*ptr==' ' || *ptr=='\t') ptr++; // skip leading whitespaces
                    if (*ptr == '#' || *ptr=='\r' || *ptr=='\n') {//comment line or empty line
                        config = rtdefault_trace_config;
                        config->updated ++;
                        continue;
                    }
                }
                printf("Syntax error in the config file %s:%d: %s\n", configFileName, line, lineBuffer);
            } else if (*ptr == 'l' && *++ptr == 'e' && *++ptr == 'x' && *++ptr == 'g' && *++ptr == 'i' && *++ptr == 'o'
                && *++ptr == 'n' && *++ptr == '.' && *++ptr == '0' && *++ptr == 'x') { //parse the [lexgion.0x1234567]
                ptr++;
                if (sscanf(ptr, "%p", &lexgion_codeptr) == 1) {
                    while(*ptr !=' ' && *ptr !='\t' && *ptr != ']') ptr++; // go to the first char after the pointer
                    while(*ptr==' ' || *ptr=='\t') ptr++; // skip leading whitespaces

                    if (*ptr == ']') {
                        ptr++;
                        while(*ptr==' ' || *ptr=='\t') ptr++; // skip leading whitespaces
                        if (*ptr == '#' || *ptr=='\r' || *ptr=='\n') {//comment line or empty line
                            config = retrieve_lexgion_config(lexgion_codeptr);
                            if (config->updated == 0) memcpy(config, rtdefault_trace_config, sizeof(lexgion_trace_config_t));
                            config->updated ++;
                            continue;
                        }
                    }
                }
                printf("Syntax error in the config file %s:%d: %s\n", configFileName, line, lineBuffer);
            }
        } else { /* read the config for a specified lexgion */
            int error = read_config_line(config, ptr);
            if (error) {
                //failed reading a line
                printf("Syntax error in the config file %s:%d: %s\n", configFileName, line, lineBuffer);
            } else {
            }
        }
    }
    fclose(configFile);
}

void print_lexgion_trace_config() {
    int i;
    printf("#=================== PInsight Lexgion Tracing Configuration ==============================\n");

    printf("#  [System Default]\n");
    lexgion_trace_config_t *config = sysdefault_trace_config;
    for (i=0; i<NUM_CONFIG_KEYS; i++) {
        printf("# \t%s = %d\n", lexgion_trace_config_keys[i], ((int*)config)[i]);
    }
    printf("[default]\n");
    config = rtdefault_trace_config;
    for (i=0; i<NUM_CONFIG_KEYS; i++) {
        printf("\t%s = %d\n", lexgion_trace_config_keys[i], ((int*)config)[i]);
    }

    int j;
    for (j=1; j<MAX_NUM_LEXGIONS; j++) {
        lexgion_trace_config_t * config = &lexgion_trace_config[j];
        if (config->codeptr != NULL) {
            printf("[lexgion.%p]\n", config->codeptr);
            for (i = 0; i < NUM_CONFIG_KEYS; i++) {
                printf("\t%s = %d\n", lexgion_trace_config_keys[i], ((int*)config)[i]);
            }
        }
    }
    printf("#==========================================================================================\n\n");
}
