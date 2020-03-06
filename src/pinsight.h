//
// Created by Yonghong Yan on 1/12/19.
//

#ifndef PINSIGHT_PINSIGHT_H
#define PINSIGHT_PINSIGHT_H
#include <stdint.h>
#include "pinsight_config.h"

/* For OpenMP, this is max number of code regions that use OpenMP directives */
#define MAX_NUM_LEXGIONS 256

/* the following two should add up 64 since we will combine the two number as uuid */
#define BITS_4_MAX_RECORDS_PER_LEXGION 32
#define BITS_4_CODEPTR_RA 32

#define MAX_RECORDS_PER_LEXGION (2<<BITS_4_MAX_RECORDS_PER_LEXGION-1)
/**
 * The trace record for a lexgion, e.g. parallel region, task, etc has a 64-bit unique ID
 * created by combining the codeptr_ra (the binary address of the runtime call of the lexgion)
 * and a counter, which is sequence number counted from 0 for the runtime instances of the lexgion.
 * The unique id (uuid) is uint64_t type of [codeptr_ra][counter] format and each of the two
 * fields (codeptr_ra and counter) is 32 bits (the above BITS_4_MAX_RECORDS_PER_LEXGION and BITS_4_CODEPTR_RA macros).
 * 32-bit uint32_t should be sufficient for the counter field (max 2**32-1, i.e. 4,294,967,295).
 * For codeptr_ra, 32-bit is likely to be ok in a 64-bit systems since most of the codeptr_ra are 24 bits.
 * The code has overflow checking and it reports error when two different codeptr_ra are truncated to the same 32-bit number.
 *
 * NOTE: we need to change the following implementation if it is not 32:32 split.
 */
#define LEXGION_RECORD_UUID(codeptr_ra, counter) (uint64_t)((uint64_t)codeptr_ra<<BITS_4_MAX_RECORDS_PER_LEXGION | counter)
#define LEXGION_RECORD_CODEPTR_RA(uuid) ((void*)(uuid>>BITS_4_MAX_RECORDS_PER_LEXGION))
#define LEXGION_RECORD_COUNTER(uuid) (uint32_t)uuid
#define CODEPTR_RA_4UUID_PREFIX(codeptr_ra) (uint32_t)(uint64_t)codeptr_ra

/* Constant or variable for controlling sampling tracing */
/* For sampling-based tracing, which allows user's control of tracing of each parallel region by specifying
 * a sampling rate, max number of traces, and initial number of traces using PINSIGHT_TRACE_CONFIG environment
 * variable. The PINSIGHT_TRACE_CONFIG should be in the form of
 * <num_initial_traces>:<max_num_traces>:<trace_sampling_rate>. Below are the example of setting PINSIGHT_TRACE_CONFIG
 * and its tracing behavior:
        PINSIGH_TRACE_CONFIG=10:50:8, This is the system default. It records the first 10 traces,
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

#define DEFAULT_NUM_INITIAL_TRACES 10
#define DEFAULT_MAX_NUM_TRACES 50
#define DEFAULT_TRACE_SAMPLING_RATE 8

extern unsigned int NUM_INITIAL_TRACES;
extern unsigned int MAX_NUM_TRACES;
extern unsigned int TRACE_SAMPLING_RATE;

typedef enum LEXGION_CLASS {
    OPENMP_LEXGION = 0,     /* OMPT, www.openmp.org */
    MPI_LEXGION,        /* P-MPI, e.g. https://www.open-mpi.org/faq/?category=perftools#PMPI */
    CUDA_LEXGION,       /* CUPTI based, https://docs.nvidia.com/cuda/cupti/index.html */
                        /* CUPTI callback already has four domains of APIs for callbacks, thus here we directly create
                         * alias for them so ...
                         */
    CUDA_CUPTI_CB_DOMAIN_DRIVER_API,
    CUDA_CUPTI_CB_DOMAIN_RUNTIME_API,
    CUDA_CUPTI_CB_DOMAIN_RESOURCE,
    CUDA_CUPTI_CB_DOMAIN_SYNCHRONIZE,
    CUDA_CUPTI_CB_DOMAIN_NVTX,  /* for Nvidia's own tools of profiling */
    OPENCL_LEXGION,     /* check clSetEventCallback, check https://www.khronos.org/registry/OpenCL/sdk/1.2/docs/man/xhtml/clSetEventCallback.html */
    ROCL_LEXGION,       /* check https://github.com/ROCm-Developer-Tools/roctracer */
    USER_LEXGION,       /* user defined API for tracing */
} LEXGION_CLASS_t;

/**
 * A lexgion (lexical region) is a region in the source code that has its footprint in
 * the runtime, e.g. a parallel region, a worksharing region, master/single region, task
 * region, target region, etc. lexgion in OpenMP are mostly translated to runtime calls.
 *
 * For MPI, each MPI method represents a lexgion
 *
 * There are so far six classes of lexgion, defined in LEXGION_CLASS_t enum. So far, we aim to implement
 * MPI_LEXGION, OPENMP_LEXGION, and CUDA_LEXGION
 *
 * A lexgion should be identified by the codeptr, the type and class together. codeptr is the binary address of the
 * first instruction of the lexgion (retun address of the runtime call), e.g. codeptr_ra in OMPT, return address of
 * an MPI call, etc. end_codeptr_ra is the binary address at the end of the lexgion, which could be the same as codeptr.
 *
 * About the codeptr_ra of different regions of OpenMP when using OMPT, check the OpenMP 5.0 standard and
 * https://github.com/passlab/pinsight/issues/41. Issues/41 includes the following info:
 *
 * For parallel, codeptr_ra for parallel_begin, parallel_end, AND barrier_begin, wait_barrier_begin,
 * wait_barrier_end and barrier_end of the implicit join barrier of the MASTER thread are the same.
 * For non-master thread, codeptr of barrier-related events for parallel join is the same, and it is NULL.
 * Thus, the codeptr_ra and end_codeptr_ra field of the lexgion objects for those are the same.
 *
 * For for (omp for or omp parallel for), master, and single, codeptr for scope_begin and score_end are different,
 * Thus codeptr and end_codeptr of the lexgion are different.
 * For master, only master thread will trigger the events. For single, all threads will trigger the events.
 *
 * for parallel for, codeptr for parallel_begin and work_begin (loop_begin) are different.
 * For single with barrier, the codeptr for single-related events and for the barrier-related events are different.
 *
 * For explicit barrier or barrier with for/single, codeptr for both begin and end events are all the same, not NULL.
 *
 * The reasons we need type for identify a lexgion are:
 * 1). OpenMP combined and composite construct, e.g. parallel for
 * 2). implicit barrier, e.g. parallel.
 * Becasue of that, the events for those constructs may use the same codeptr_ra for the callback, thus we need further
 * check the type so we know whether we need to create two different lexgion objects
 * */
typedef struct lexgion {
    /* we use the binary address and type of the lexgion as key for each lexgion.
     * A code region may be entered from multiple callpath.
     */
    const void *codeptr_ra; /* the codeptr_ra at the beginning of the lexgion */
    int class; /* the class of the lexgion, which could be OPENMP_LEXGION, MPI_LEXGION, CUDA_LEXGION */
    int type; /* the type of a lexgion in the class: e.g. parallel, master, singer, barrier, task, section, etc for OPENMP.
               * For OpenMP, we use trace record event id as the type of each lexgion; For MPI, we define a macro for each MPI methods */
    const void *end_codeptr_ra; /* the codeptr_ra at the end of the lexgion */
    /* we need volatile and atomic inc this counter only in the situation where two master threads enter into the same region */
    volatile unsigned int counter; /* counter for total number of execution of the region */
    unsigned int trace_counter; /* counter for total number of traced execution */
    struct lexgion_trace_config * trace_config;
    unsigned int num_exes_after_last_trace; /* counter to control the sampling */
} lexgion_t;

/**
 * the struct for storing trace config of a lexgion. trace config can be updated at the runtime so PInsight can do dynamic
 * tracing. Each time, PInsight needs to check the config of a lexgion, it checks the lexgion_trace_config object
 * to find out the current trace rate, etc.
 *
 * All threads share a single config for each lexgion. Thus for each lexgion, tracing behavior for all threads are the same.
 */
typedef struct lexgion_trace_config {
    volatile void *codeptr_ra; /* the codeptr of the lexgion */
    volatile void * dummy;  /* NOTE: make sure I do not run into problems of compiling the code for 32 bit systems since the codeptr_ra
                             * used __sync_bool_compare_and_swap to access it */
    int trace_enable; /* dynamic tracing: to enable or disable tracing of the lexgion */

    /* the following are for controlling tracing rate if trace is enabled */
    /* counter and count for sampling and fine-grained control of tracing. So far, we only do trace control for parallel region.
     * If a parallel region is traced, all constructs insides are traced. */
    unsigned int num_initial_traces;
    int sample_rate; /* the rate of tracing. Every sample_rate executions of the region, a trace is recorded.
                      * 0: no tracing; -1: trace all */
    unsigned int max_num_traces;
} lexgion_trace_config_t;

/* This macro check whether to trace or not: trace_bit is set if trace_enable is set AND
 * traces for the initial number of exes or when reaching sampling rate */
#define lexgion_set_trace_bit(lgp) {trace_bit = lgp->trace_config->trace_enable && (lgp->trace_counter < lgp->trace_config->num_initial_traces || \
                            (lgp->trace_counter < lgp->trace_config->max_num_traces && (lgp->trace_config->sample_rate == -1 || \
                                                    lgp->num_exes_after_last_trace == lgp->trace_config->sample_rate)));}

#define lexgion_post_trace_update(lgp) {lgp->trace_counter++; lgp->num_exes_after_last_trace=0;}

/* the max depth of nested lexgion, 16 should be enough if we do not have recursive such as in OpenMP tasking */
#define MAX_LEXGION_STACK_DEPTH 16
/**
 * the thread-local object that store data for each thread
 */
typedef struct pinsight_thread_data {
    /* the stack for storing the lexgion */
    //lexgion_t *lexgion_stack;
    int initial_thread;

    /* the runtime stack for lexgion instances */
    struct lexgion_stack {
        lexgion_t *lgp;
        unsigned int counter; /* this counter is the counter of lgp when an lexgion instance is created and
                               * pushed to the stack */
        int tracing; /* a flag to indicate whether to trace this instance or not */
    } lexgion_stack[MAX_LEXGION_STACK_DEPTH];
    int stack_top;

    /* this is the lexgion cache runtime stores, a lexgion is added to the array when the runtime encounters it.
     * The lexgion counter is updated when the lexgion is instantiated at the runtime */
    lexgion_t lexgions[MAX_NUM_LEXGIONS]; /* the array to store all the lexgions */
    int num_lexgions; /* the last lexgion in the lexgion array */
    int recent_lexgion; /* the most-recently used lexgion in the lexgion array */
} pinsight_thread_data_t;

/* information to put in the event records */
extern __thread int global_thread_num;
extern __thread int omp_thread_num;
extern __thread const void * parallel_codeptr;
extern __thread unsigned int parallel_counter;
extern __thread const void * task_codeptr;
extern __thread unsigned int task_counter;

extern __thread pinsight_thread_data_t pinsight_thread_data;
extern __thread int trace_bit; /* 0 or 1 for enabling trace */
extern lexgion_trace_config_t lexgion_trace_config[]; //all threads share a single config for each lexgion */

#define recent_lexgion() (pinsight_thread_data.lexgions[pinsight_thread_data.lexgion_recent])

#ifdef  __cplusplus
extern "C" {
#endif

extern pinsight_thread_data_t * init_thread_data(int _thread_num);
extern void push_lexgion(lexgion_t * lexgion, unsigned int counter);
extern lexgion_t * pop_lexgion(unsigned int * counter);
extern lexgion_t * top_lexgion_type(int class, int type, unsigned int * counter);
extern lexgion_t * top_lexgion(unsigned int * counter);
extern lexgion_t *lexgion_begin(int class, int type, const void *codeptr_ra);
extern lexgion_t *lexgion_end(unsigned int * counter);
extern void set_initial_lexgion_trace_config();

#ifdef  __cplusplus
};
#endif

#endif //PINSIGHT_PINSIGHT_H
