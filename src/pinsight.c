#include <stdio.h>
#include "pinsight.h"

/* these are thread-local storage assuming that OpenMP runtime threads are 1:1 mapped to
 * the system threads (PThread for example). If not, we should implement our own OpenMP
 * thread-local storage (not system TLS).
 */
__thread int global_thread_num;
__thread int omp_thread_num;
__thread const void * parallel_codeptr;
__thread unsigned int parallel_counter;
__thread const void * task_codeptr;
__thread unsigned int task_counter;
__thread pinsight_thread_data_t pinsight_thread_data;
__thread ompt_lexgion_t * implicit_task;
__thread int trace_bit; /* 0 or 1 for enabling trace */

unsigned int NUM_INITIAL_TRACES = DEFAULT_NUM_INITIAL_TRACES;
unsigned int MAX_NUM_TRACES = DEFAULT_MAX_NUM_TRACES;
unsigned int TRACE_SAMPLING_RATE = DEFAULT_TRACE_SAMPLING_RATE;

/** init thread data
 */
pinsight_thread_data_t * init_thread_data(int _thread_num) {
    global_thread_num = _thread_num;
//    pinsight_thread_data.thread_type = thread_type;
    pinsight_thread_data.stack_top = -1;
    pinsight_thread_data.num_lexgions = 0;
    pinsight_thread_data.recent_lexgion = -1;

    return &pinsight_thread_data;
}

void push_lexgion(ompt_lexgion_t * lgp, unsigned int counter) {
    int top = pinsight_thread_data.stack_top+1;
    if (top == MAX_LEXGION_STACK_DEPTH) {
       fprintf(stderr, "thread %d lexgion stack overflow\n", global_thread_num);
       return;
    }
    pinsight_thread_data.lexgion_stack[top].lgp = lgp;
    pinsight_thread_data.lexgion_stack[top].counter = counter;
    pinsight_thread_data.stack_top++;
}

ompt_lexgion_t * pop_lexgion(unsigned int * counter) {
    int top = pinsight_thread_data.stack_top;
    ompt_lexgion_t * lgp = pinsight_thread_data.lexgion_stack[top].lgp;
    if (counter != NULL) *counter = pinsight_thread_data.lexgion_stack[top].counter;
    pinsight_thread_data.stack_top--;
    return lgp;
}

/**
 * this can be called by multiple threads since it is a read-only search.
 * @param codeptr_ra
 * @param index
 * @return
 */
static ompt_lexgion_t *ompt_find_lexgion(int type, const void *codeptr_ra, int * index) {
    /* play it safe for dealing with data race */
    if (pinsight_thread_data.recent_lexgion < 0 || pinsight_thread_data.num_lexgions <= 0) return NULL;
    int i;
    ompt_lexgion_t * lgp;
    /* search forward from the most recent one */
    for (i=pinsight_thread_data.recent_lexgion; i<pinsight_thread_data.num_lexgions; i++) {
        if (type == pinsight_thread_data.lexgions[i].type && pinsight_thread_data.lexgions[i].codeptr_ra == codeptr_ra) {
            *index = i;
            lgp = &pinsight_thread_data.lexgions[i];
            return lgp;
        } else if (codeptr_ra != pinsight_thread_data.lexgions[i].codeptr_ra) {
            /* this is where we check whether the uuid approach will work or not */
            if (CODEPTR_RA_4UUID_PREFIX(codeptr_ra) == CODEPTR_RA_4UUID_PREFIX(pinsight_thread_data.lexgions[i].codeptr_ra)) {
                fprintf(stderr, "Two different codeptr_ra (%p and %p) are rounded to the same UUID prefix\n",
                codeptr_ra, pinsight_thread_data.lexgions[i].codeptr_ra);
            }
        }
    }
    /* search from 0 to most recent one */
    for (i=0; i<pinsight_thread_data.recent_lexgion; i++) {
        if (pinsight_thread_data.lexgions[i].codeptr_ra == codeptr_ra && type == pinsight_thread_data.lexgions[i].type) {
            *index = i;
            lgp = &pinsight_thread_data.lexgions[i];
            return lgp;
        } else if (codeptr_ra != pinsight_thread_data.lexgions[i].codeptr_ra) {
            /* this is where we check whether the uuid approach will work or not */
            if (CODEPTR_RA_4UUID_PREFIX(codeptr_ra) == CODEPTR_RA_4UUID_PREFIX(pinsight_thread_data.lexgions[i].codeptr_ra)) {
                fprintf(stderr, "Two different codeptr_ra (%p and %p) are rounded to the same UUID prefix\n",
                        codeptr_ra, pinsight_thread_data.lexgions[i].codeptr_ra);
            }
        }
    }
    return NULL;
}

/**
 * This is a thread-specific call
 *
 */
ompt_lexgion_t *ompt_lexgion_begin(int type, const void *codeptr_ra) {
    int index;

    ompt_lexgion_t *lgp = ompt_find_lexgion(type, codeptr_ra, &index);
    if (lgp == NULL) {
        index = pinsight_thread_data.num_lexgions;
        if (index == MAX_NUM_LEXGIONS) {
            fprintf(stderr, "Max number of lex regions (%d) allowed in the source code reached\n",
                    MAX_NUM_LEXGIONS);
            return NULL;
        }
        lgp = &pinsight_thread_data.lexgions[index];
        lgp->codeptr_ra = codeptr_ra;
        //printf("%d: lexgion_begin(%d, %X): first time encountered %X\n", thread_id, i, codeptr_ra, lgp);
        lgp->type = type;

        /* init counters for number of exes, traces, and sampling */
        lgp->counter = 0;
        lgp->num_exes_after_last_trace = 0;
        lgp->sample_rate = TRACE_SAMPLING_RATE;
        lgp->trace_counter = 0;
        lgp->max_num_traces = MAX_NUM_TRACES;
        lgp->num_initial_traces = NUM_INITIAL_TRACES;

        pinsight_thread_data.num_lexgions++;
    }
    pinsight_thread_data.recent_lexgion = index; /* cache it for future search */
    return lgp;
}

/**
 * Mark a lexgion end
 * @param type
 * @param codeptr_ra
 * @return
 */
ompt_lexgion_t *ompt_lexgion_end(unsigned int * counter) {
    return pop_lexgion(counter);
}

/**
 * search the lexgion stack to find the topmost lexgion instance in the stack of the specified type. The counter of the
 * lexgion instance is returned via the counter argument
 * @param type
 * @param counter
 * @return
 */
ompt_lexgion_t * top_lexgion_type(int type, unsigned int * counter) {
    int top = pinsight_thread_data.stack_top;
    while (top >=0) {
        ompt_lexgion_t *lgp = pinsight_thread_data.lexgion_stack[top].lgp;
        if (lgp->type == type) {
            if (counter != NULL) *counter = pinsight_thread_data.lexgion_stack[top].counter;
            return lgp;
        }
        top--;
    }
    return NULL;
}

ompt_lexgion_t * top_lexgion(unsigned int * counter) {
    int top = pinsight_thread_data.stack_top;
    if (top >=0) {
        ompt_lexgion_t *lgp = pinsight_thread_data.lexgion_stack[top].lgp;
        if (counter != NULL) *counter = pinsight_thread_data.lexgion_stack[top].counter;
        return lgp;
    }
    return NULL;
}
