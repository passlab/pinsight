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

#define NUM_CONFIG_KEYS 9
//Five features so far: OpenMP, MPI, CUDA, ENERGY, and BACKTRACE
#define NUM_CONFIG_FEATURES 5
static const char *lexgion_trace_config_keys[] = {
        "OpenMP_trace_enabled",   //true or false: to enable/disable OpenMP trace
        "MPI_trace_enabled",      //true or false: to enable/disable MPI trace
        "CUDA_trace_enabled",     //true or false: to enable/disable CUDA trace
        "ENERGY_trace_enabled",   //true or false: to enable/disable energy tracing
        "BACKTRACE_enabled",      //true or false: to enable/disable backtrace.
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
 * For each lexgion, the configuration can be viewed as key-value pairs and the configuration
 * file should be written in the format similar to, but simpler than INI or TOML format.
 *
 * In this implementation, there are two restrictions: 1) each field of this struct MUST be "int" type, and
 * 2) the fields in lexgion_trace_config MUST BE in the same order as the keys defined in the lexgion_trace_config_keys[].
 * This is designed to simplify parsing a configuration file to read the configuration values. With this restriction,
 * to add, remove or change a config option, you need to follow the two restriction to change the NUM_CONFIG_KEYS,
 * lexgion_trace_config_keys, and lexgion_trace_config. And then modify the lexgion_trace_config_sysdefault function for
 * the changes. With that, we do not need to change the code for reading the config file.
 *
 * The system default of the config represents the features that are enabled when the library is built.
 * The default value for each feather key (*_enabled) is set according to whether the feature is enabled
 * when this library is built. A feature can only be enabled at the runtime if it is default enabled (which means the
 * library is built to support this feature). Thus if a feature is not enabled by system default, it cannot be enabled by runtime
 * configuration.
 */
typedef struct lexgion_trace_config {  /* all the config fields MUST be from the beginning and
                                        * in the same order as the keys in the config_keys array */
    int OpenMP_trace_enabled;            //true or false: to enable/disable OpenMP trace
    int MPI_trace_enabled;            //true or false: to enable/disable MPI trace
    int CUDA_trace_enabled;           //true or false: to enable/disable CUDA trace
    int ENERGY_trace_enabled;          //true or false: to enable/disable energy tracing
    int BACKTRACE_enabled;             //true or false: to enable backtrace or not.
    unsigned int trace_starts_at;      //integer: the number of execution of the region before tracing starts.
    unsigned int initial_trace_count;  //integer: the number of traces to be collected after the trace starts the first time
    unsigned int max_num_traces;       //integer: total number of traces to be collected
    unsigned int tracing_rate;         //integer: the rate an execution is traced, e.g. 10 for 1 trace per 10 execution of the region.
    
    const void *codeptr;               //codeptr as key for region-specific configuration
    volatile void * dummy;             /* NOTE: make sure I do not run into problems of compiling the code for 32 bit systems since the codeptr_ra
                                        * used __sync_bool_compare_and_swap to access it */
    int updated;                       //a flag to indicate whether a config is updated by the ongoing reading from the config file. It is used only for updating
} lexgion_trace_config_t;

#endif // LEXGION_TRACE_CONFIG_H
