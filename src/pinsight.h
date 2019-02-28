//
// Created by Yonghong Yan on 1/12/19.
//

#ifndef PINSIGHT_PINSIGHT_H
#define PINSIGHT_PINSIGHT_H

#include <ompt.h>

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

/**
 * A lexgion (lexical region) is a region in the source code that has its footprint in
 * the runtime, e.g. a parallel region, a worksharing region, master/single region, task
 * region, target region, etc. lexgion in OpenMP are mostly translated to runtime calls.
 *
 * A lexgion should be identified by the codeptr_ra of OMPT callback and the type field together.
 * codeptr_ra is the binary address at the beginning of lexgion and type is the type of region.
 * end_codeptr_ra is the binary address at the end of the lexgion.
 *
 * About the codeptr_ra of different regions, check the OpenMP 5.0 standard and
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
typedef struct ompt_lexgion {
    /* we use the binary address and type of the lexgion as key for each lexgion.
     * A code region may be entered from multiple callpath.
     */
    const void *codeptr_ra; /* the codeptr_ra at the beginning of the lexgion */
    int type; /* the type of a lexgion: parallel, master, singer, barrier, task, section, etc. we use trace record event id for this type */
    const void *end_codeptr_ra; /* the codeptr_ra at the end of the lexgion */
    /* we need volatile and atomic inc this counter only in the situation where two master threads enter into the same region */
    volatile unsigned int counter; /* total number of records, i.e. about total number of execution of the same region */
} ompt_lexgion_t;

/* the max depth of nested lexgion, 16 should be enough if we do not have recursive such as in OpenMP tasking */
#define MAX_LEXGION_STACK_DEPTH 16
/**
 * the thread-local object that store data for each thread
 */
typedef struct pinsight_thread_data {
    ompt_thread_t thread_type;
    /* the stack for storing the lexgion */
    //ompt_lexgion_t *lexgion_stack;

    /* the runtime stack for lexgion instances */
    struct ompt_lexgion_stack {
        ompt_lexgion_t *lgp;
        unsigned int counter; /* this counter is the counter of lgp when an lexgion instance is created and
                               * pushed to the stack */
    } lexgion_stack[MAX_LEXGION_STACK_DEPTH];
    int stack_top;

    /* this is the lexgion cache runtime stores, a lexgion is added to the array when the runtime encounters it.
     * The lexgion counter is updated when the lexgion is instantiated at the runtime */
    ompt_lexgion_t lexgions[MAX_NUM_LEXGIONS]; /* the array to store all the lexgions */
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

#define recent_lexgion() (pinsight_thread_data.lexgions[pinsight_thread_data.lexgion_recent])

#ifdef  __cplusplus
extern "C" {
#endif

extern pinsight_thread_data_t * init_thread_data(int _thread_num, ompt_thread_t thread_type);
extern void push_lexgion(ompt_lexgion_t * lexgion, unsigned int counter);
extern ompt_lexgion_t * pop_lexgion(unsigned int * counter);
extern ompt_lexgion_t * top_lexgion_type(int type, unsigned int * counter);
extern ompt_lexgion_t * top_lexgion(unsigned int * counter);
extern ompt_lexgion_t *ompt_lexgion_begin(int type, const void *codeptr_ra);
extern ompt_lexgion_t *ompt_lexgion_end(unsigned int * counter);

#ifdef  __cplusplus
};
#endif

#endif //PINSIGHT_PINSIGHT_H
