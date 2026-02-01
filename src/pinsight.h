//
// Created by Yonghong Yan on 1/12/19.
//

#ifndef PINSIGHT_PINSIGHT_H
#define PINSIGHT_PINSIGHT_H
#include <stdint.h>
#include "pinsight_config.h"
#include "trace_config.h"

/* For OpenMP, this is max number of code regions that use OpenMP directives */
#define MAX_NUM_LEXGIONS 256

//TODO: rename it to domain, and use bit-operation to check domain. FOr that, each domain will be numbered as 0, 2, 4, 8, 16, ...
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
 * Because of that, the events for those constructs may use the same codeptr_ra for the callback, thus we need further
 * check the type so we know whether we need to create two different lexgion objects
 *
 * This is per-thread data structure, i.e. for the same code region, an object is created for each thread when it is needed.
 * No two threads share the same object of the same code region
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
    trace_config_t *trace_config;
    unsigned int num_exes_after_last_trace; /* counter to control the sampling based on trace rate */

    //fields for logging purpose
    /* we need volatile and atomic inc this counter only in the situation where two master threads enter into the same region */
    volatile unsigned int counter; /* counter for total number of execution of the region */
    unsigned int trace_counter; /* counter for total number of traced execution */
    unsigned int first_trace_num; //the execution number when the first trace is recorded
    unsigned int last_trace_num; //the execution number when the last trace is recorded
} lexgion_t;

/* This macro check whether to trace or not: trace_bit is set if trace_enable is set AND
 * traces for the initial number of exes or when reaching sampling rate */
#define lexgion_set_trace_bit(lgp) {trace_bit = lgp->trace_config->OpenMP_trace_enabled && (lgp->trace_config->trace_starts_at <= lgp->counter-1) && \
                            (lgp->trace_counter < lgp->trace_config->initial_trace_count || \
                            (lgp->trace_counter < lgp->trace_config->max_num_traces && lgp->num_exes_after_last_trace == lgp->trace_config->tracing_rate));}

#define lexgion_post_trace_update(lgp) {lgp->trace_counter++; if (lgp->trace_counter == 1) lgp->first_trace_num = lgp->counter-1; lgp->num_exes_after_last_trace=0; lgp->last_trace_num = lgp->counter-1;}

/**
 * The runtime instance/frame/record of a lexgion of a thread
 */
typedef struct lexgion_record_t {
    lexgion_t *lgp;
    unsigned int record_id; /* the record_id is the counter of lgp when an lexgion instance is created and
                           * pushed to the stack */
   int tracing; /* a flag to indicate whether to trace this instance or not */
} lexgion_record_t;

/* The system-level default trace config for each domain */
extern trace_config_t trace_config[];

/* the max depth of nested lexgion, 16 should be enough if we do not have recursive such as in OpenMP tasking */
#define MAX_LEXGION_STACK_DEPTH 16
/**
 * the thread-local object that store data for each thread
 */
typedef struct pinsight_thread_data {
	int initialized;    //flag to indicate whether the thread data is initialized or not
	int initial_parallel_task_initialized_flag; //flag to indicate whether the initial parallel region and task are initialized or not, only for the initial thread
    int initial_thread; //flag to indicate whether this is initial thread or not

    /* the runtime stack for lexgion instances */
    struct lexgion_record_t lexgion_stack[MAX_LEXGION_STACK_DEPTH];
    int stack_top;

    /* this is the lexgion cache runtime stores, a lexgion is added to the array when the runtime encounters it.
     * The lexgion counter is updated when the lexgion is instantiated at the runtime */
    lexgion_t lexgions[MAX_NUM_LEXGIONS]; /* the array to store all the lexgions */
    int num_lexgions; /* the last lexgion in the lexgion array */
    int recent_lexgion; /* the most-recently used lexgion in the lexgion array */

    //thread local
    int global_thread_num;
    int omp_team_num;
    int omp_thread_num;
    lexgion_record_t * enclosing_parallel_lexgion_record;
    lexgion_record_t * enclosing_task_lexgion_record;

    trace_config_t *trace_config; //The trace config for this the thread
} pinsight_thread_data_t;

/* information to put in the event records */
extern __thread int global_thread_num;
extern __thread int omp_thread_num;
extern __thread lexgion_record_t * enclosing_parallel_lexgion_record;
extern __thread lexgion_record_t * enclosing_task_lexgion_record;

//These are used for lttng to copy to the trace record, they are redundant
extern __thread const void * parallel_codeptr;
extern __thread unsigned int parallel_record_id;
extern __thread const void * task_codeptr;
extern __thread unsigned int task_record_id;

extern __thread pinsight_thread_data_t pinsight_thread_data;
extern __thread int trace_bit; /* 0 or 1 for enabling trace */
//extern lexgion_trace_config_t* lexgion_trace_config; //all threads share a single config for each lexgion */

#define recent_lexgion() (pinsight_thread_data.lexgions[pinsight_thread_data.lexgion_recent])

#ifdef  __cplusplus
extern "C" {
#endif

extern pinsight_thread_data_t * init_thread_data(int _thread_num);
extern lexgion_record_t * push_lexgion(lexgion_t * lexgion, unsigned int record_id);
extern lexgion_t * pop_lexgion(unsigned int * record_id);
extern lexgion_record_t * top_lexgion() ;
extern lexgion_record_t * top_lexgion_type(int class, int type);
extern lexgion_record_t *lexgion_begin(int class, int type, const void *codeptr_ra);
extern lexgion_t *lexgion_end(unsigned int * record_id);

//implemented in lexgion_trace_cnofig.c
extern trace_config_t * retrieve_lexgion_trace_config(const void * codeptr);
//read env or config file at runtime to allow for user to provide new config for tracing
extern void lexgion_trace_reconfig();

#ifdef  __cplusplus
};
#endif

#endif //PINSIGHT_PINSIGHT_H
