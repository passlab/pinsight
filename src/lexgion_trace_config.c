// PInsight environment variable querying functions.
// Copyright (c) 2018, PASSLab Team. All rights reserved.
#include <stdio.h>
#include <string.h>
#include "pinsight.h"

lexgion_trace_config_t lexgion_trace_config[MAX_NUM_LEXGIONS+1]; //The first one is used for default

/* Constant or variable for controlling sampling tracing */
/* For sampling-based tracing, which allows user's control of tracing of each parallel region by specifying
 * a sampling rate, max number of traces, and initial number of traces using PINSIGHT_TRACE_CONFIG environment
 * variable. The PINSIGHT_TRACE_CONFIG should be in the form of
 * <num_initial_traces>:<max_num_traces>:<trace_sampling_rate>. Below are the example of setting PINSIGHT_TRACE_CONFIG
 * and its tracing behavior:
        PINSIGH_TRACE_CONFIG=10:50:10, This is the system default. It records the first 10 traces,
                then after that, record one trace per 10 execution and in total max 50 traces should be recorded.
        PINSIGH_TRACE_CONFIG=<any_number>:-1:-1,  Record all the traces.
        PINSIGHT_TRACE_CONFIG=0:-1:10, record 1 trace per 10 executions for all the executions.
        PINSIGHT_TRACE_CONFIG=20:20:-1, record the first 20 iterations

 * In implementation, there are three global variables for the three configuration variables: NUM_INITIAL_TRACES,
 * MAX_NUM_TRACES, TRACE_SAMPLING_RATE.
 * NUM_INITIAL_TRACES specifies how many traces must be recorded from the beginning of the execution of the region.
 * MAX_NUM_TRACES specifies the total number of trace records LTTng will collect for the region.
 * TRACE_SAMPLING_RATE specifies the rate a trace will be recorded, e.g. every TRACE_SAMPLING_RATE of
 * executions of the region, a trace is recorded.
 * The three variables will be initialized from PINSIGHT_TRACE_CONFIG environment variable when OMPT is initialized.
 * For each region, they are copied to the corresponding variables of each region, thus the implementation has the
 * capability of setting different trace configuration for different regions.
 */

static void lexgion_trace_config_sysdefault() {
    //set the default for the global config
    lexgion_trace_config[0].trace_enabled = 1;
    lexgion_trace_config[0].ompt_trace_enabled = 1;
    lexgion_trace_config[0].pmpi_trace_enabled = 0;
    lexgion_trace_config[0].cupti_trace_enabled = 0;
    lexgion_trace_config[0].trace_starts_at = 0;
    lexgion_trace_config[0].initial_trace_count = 10;
    lexgion_trace_config[0].max_num_traces = -1;
    lexgion_trace_config[0].tracing_rate = 1;

    lexgion_trace_config[0].codeptr = NULL;
}

/**
 * This function is not thread safe, but it seems won't hurt calling by multiple threads
 */
__attribute__ ((constructor)) void initial_lexgion_trace_config() {
    lexgion_trace_config_sysdefault();
    lexgion_trace_config_read();
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
            //we have enough entries (MAX_NUM_LEXGIONS) for the posibble number of lexgions in the app.
            //otherwise, this is fault.

        }
    }
    return config;
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
            if (strcasecmp(value, "TRUE") == 0) {
                ((int*)config)[i] = 1;
            } else if (strcasecmp(value, "FALSE") == 0) {
                ((int*)config)[i] = 0;
            } else {
                ((int*)config)[i] = atoi(value);
            }
            //printf("%s=%d\n", key, ((unsigned int*)config)[i]);
            return 0;
        }
    }
    return 1;
}

void lexgion_trace_config_copy(lexgion_trace_config_t * dest, lexgion_trace_config_t * src) {
    dest->trace_enabled = src->trace_enabled;
    dest->ompt_trace_enabled = src->ompt_trace_enabled;
    dest->pmpi_trace_enabled = src->pmpi_trace_enabled;
    dest->cupti_trace_enabled = src->pmpi_trace_enabled;
    dest->trace_starts_at = src->trace_starts_at;
    dest->initial_trace_count = src->initial_trace_count;
    dest->max_num_traces = src->max_num_traces;
    dest->tracing_rate = src->tracing_rate;
}

/**
 * Read the config file and store the configuration into a lexgion_trace_config_t object
 */
void lexgion_trace_config_read() {
    /* read in config from config file if it is provided via lexgion_trace_configFILE env */
    const char* configFileName = getenv("PINSIGHT_LEXGION_TRACE_CONFIG");
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
    int freshnewconfig = 1;

    /* Read the config file and store the configuration into a lexgion_trace_config_t object */
    lexgion_trace_config_t *config; /* The very first one should be initialized from the system */
    lexgion_trace_config_t *global_config = &lexgion_trace_config[0];
    char lineBuffer[512];
    int max_char = 511;
    void * lexgion_codeptr;
    int read_global_config = 1; /* a flag */
    int line = 0;
    int col = 1;
    int index;
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
                        config = global_config;
                        config->updated = 1;
                        index = 0;
                        continue;
                    }
                }
                printf("Syntax error in the config file %s:%d\n", configFileName, line);
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
                            config->updated = 1;
                            if (freshnewconfig) lexgion_trace_config_copy(config, global_config); //copy the default config first
                            continue;
                        }
                    }
                }
                printf("Syntax error in the config file %s:%d\n", configFileName, line);
            }
        } else { /* read the config for a specified lexgion */
            int error = read_config_line(config, ptr);
            if (error) {
                //failed reading a line
                printf("Syntax error in the config file %s:%d\n", configFileName, line);
            } else {
            }
        }
    }
    fclose(configFile);

    if (freshnewconfig) {
        int i;
        for (i=0; i<MAX_NUM_LEXGIONS; i++) {
            config = &lexgion_trace_config[i];
            if (config->updated) {
                config->updated = 0;
            } else if (i > 0) { //copy from the global config since this will be the new config to be used in the future
                lexgion_trace_config_copy(config, global_config);
            }
        }
    }
}

void print_lexgion_trace_config() {
    int i;
    printf("\n=================== PInsight Lexgion Tracing Configuration ==============================\n");
    printf("[default]\n");
    lexgion_trace_config_t *config = &lexgion_trace_config[0];

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
    printf("==========================================================================================\n\n");
}
