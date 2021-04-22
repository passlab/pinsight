// PInsight environment variable definitions, and safe querying functions.
// Copyright (c) 2018, PASSLab Team. All rights reserved.

#ifndef LEXGION_TRACE_CONFIG_H
#define LEXGION_TRACE_CONFIG_H

#include <errno.h>
#include <stdlib.h>

// --------------------------------------------------------
// Environment config variables

#define PINSIGHT_DEBUG "PINSIGHT_DEBUG"
#define PINSIGHT_DEBUG_DEFAULT 0

// --------------------------------------------------------
// Safe environment variable query functions

long env_get_long(const char* varname, long default_value);
unsigned long env_get_ulong(const char* varname, unsigned long default_value);

#define NUM_CONFIG_KEYS 8
// Must be in the same order as the config_init struct
static const char *lexgion_trace_config_keys[] = {
        "trace_enabled",          //true or false: for the first and global check before others, default TRUE 
        "ompt_trace_enabled",     //true or false: to enable/disable OpenMP trace, default TRUE 
        "pmpi_trace_enabled",     //true or false: to enable/disable MPI trace, default FALSE
        "cupti_trace_enabled",    //true or false: to enable/disable CUDA trace, default FALSE
        "trace_starts_at",        //integer: the number of execution of the region before tracing starts. 
        "initial_trace_count",    //integer: the number of traces to be collected after the trace starts the first time
        "max_num_traces",         //integer: total number of traces to be collected
        "tracing_rate",           //integer: the rate an execution is traced, e.g. 10 for 1 trace per 10 execution of the region. 
};

/**
 * the struct for storing trace config of a lexgion. trace config can be updated at the runtime so PInsight can do dynamic
 * tracing. Each time, PInsight needs to check the config of a lexgion, it checks the lexgion_trace_config object
 * to find out the current trace rate, etc.
 *
 * All threads share a single config for each lexgion. Thus for each lexgion, tracing behavior for all threads are the same.
 * 
 * This struct is used for providing configuration for tracing a lexgion, which could be an OpenMP/MPI/CUPTI region
 * Each lexgion has an object of it and there is an global object of this struct as well to provide the default configuration.
 *
 * Each field of this struct MUST be "int" type, which is designed to simplify parsing a configuration file to read
 * the configuration values. For each lexgion, the configuration can be viewed as key-value pairs and the configuration
 * file will be written in the format similar to, but simpler than INI or TOML format. 
 * The order of those fields in the pinsight_config_t MUST be in the same order as their keys in the config_key array, 
 * again, it is designed to simplify parsing the config file. Thus to add or remove a configuration, 
 * pinsight_config_t, NUM_CONFIG_KEYS and pinsight_config_keys must
 * be modified and make sure they are consistent so we do not need to change the code for reading the config file.
 */
typedef struct lexgion_trace_config {/* all the config fields MUST be from the beginning and
                              * in the same order as the keys in the config_keys array */
    int trace_enabled;          //true or false: for the first and global check before others, default TRUE 
    int ompt_trace_enabled;      //true or false: to enable/disable OpenMP trace, default TRUE 
    int pmpi_trace_enabled;     //true or false: to enable/disable MPI trace, default FALSE
    int cupti_trace_enabled;    //true or false: to enable/disable CUDA trace, default FALSE
    unsigned int trace_starts_at;         //integer: the number of execution of the region before tracing starts. 
    unsigned int initial_trace_count;   //integer: the number of traces to be collected after the trace starts the first time
    unsigned int max_num_traces;         //integer: total number of traces to be collected
    unsigned int tracing_rate;           //integer: the rate an execution is traced, e.g. 10 for 1 trace per 10 execution of the region. 
    
    const void *codeptr;    //codeptr as key
    volatile void * dummy;  /* NOTE: make sure I do not run into problems of compiling the code for 32 bit systems since the codeptr_ra
                             * used __sync_bool_compare_and_swap to access it */
    int updated; //a flag to indicate whether a config is updated by the ongoing reading from the config file. It is used only for updating
} lexgion_trace_config_t;

#endif // LEXGION_TRACE_CONFIG_H
