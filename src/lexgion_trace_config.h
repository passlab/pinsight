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

typedef enum lexgion_trace_config_key_index {
	OpenMP_trace_enabled_index = 0,
    MPI_trace_enabled_index,
    CUDA_trace_enabled_index,
    ENERGY_trace_enabled_index,
    BACKTRACE_enabled_index,
    trace_starts_at_index,
    initial_trace_count_index,
    max_num_traces_index,
    tracing_rate_index,
} lexgion_trace_config_key_index_t;

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
    int use_default;                   //a flag to indicate whether a config is copied from the rtdefault or set by the user provided in the config file.
} lexgion_trace_config_t;

/**
 * This struct maintains the event/callbacks that are implemented and enabled/disabled at the runtime for runtime to perform fine-grainularity control of
 * which events are to be traced and which events can be skipped
 *
 * It should list the same as the following enum defined in omp-tools.h file (https://github.com/llvm/llvm-project/blob/main/openmp/runtime/src/include/omp-tools.h.var#L214)
 *
 * typedef enum ompt_callbacks_t {
  ompt_callback_thread_begin             = 1,
  ompt_callback_thread_end               = 2,
  ompt_callback_parallel_begin           = 3,
  ompt_callback_parallel_end             = 4,
  ompt_callback_task_create              = 5,
  ompt_callback_task_schedule            = 6,
  ompt_callback_implicit_task            = 7,
  ompt_callback_target                   = 8,
  ompt_callback_target_data_op           = 9,
  ompt_callback_target_submit            = 10,
  ompt_callback_control_tool             = 11,
  ompt_callback_device_initialize        = 12,
  ompt_callback_device_finalize          = 13,
  ompt_callback_device_load              = 14,
  ompt_callback_device_unload            = 15,
  ompt_callback_sync_region_wait         = 16,
  ompt_callback_mutex_released           = 17,
  ompt_callback_dependences              = 18,
  ompt_callback_task_dependence          = 19,
  ompt_callback_work                     = 20,
  ompt_callback_master     DEPRECATED_51 = 21,
  ompt_callback_masked                   = 21,
  ompt_callback_target_map               = 22,
  ompt_callback_sync_region              = 23,
  ompt_callback_lock_init                = 24,
  ompt_callback_lock_destroy             = 25,
  ompt_callback_mutex_acquire            = 26,
  ompt_callback_mutex_acquired           = 27,
  ompt_callback_nest_lock                = 28,
  ompt_callback_flush                    = 29,
  ompt_callback_cancel                   = 30,
  ompt_callback_reduction                = 31,
  ompt_callback_dispatch                 = 32,
  ompt_callback_target_emi               = 33,
  ompt_callback_target_data_op_emi       = 34,
  ompt_callback_target_submit_emi        = 35,
  ompt_callback_target_map_emi           = 36,
  ompt_callback_error                    = 37
} ompt_callbacks_t;
 *
 */
typedef struct omp_trace_config_t {
	int event_callback; 	//The enum value of the callback
	unsigned int implemented;  	//Whether the callback is implemented by PInsight
	unsigned int enabled;    //Whether the callback is enabled or not
} omp_trace_config_t;

typedef struct mpi_trace_config_t {

} mpi_trace_config_t;

typedef struct cuda_trace_config_t {

} cuda_trace_config_t;

#endif // LEXGION_TRACE_CONFIG_H
