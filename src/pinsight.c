#include <stdio.h>
#include <stdlib.h>
#include "pinsight.h"

__thread pinsight_thread_data_t pinsight_thread_data;
__thread int trace_bit = 0; /* 0 or 1 for enabling trace */

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

/**
 * push the encounting lexgion to the stack, also saving the counter of the lexgion instance
 * @param lgp
 * @param counter
 */
void push_lexgion(lexgion_t * lgp, unsigned int counter) {
    int top = pinsight_thread_data.stack_top+1;
    if (top == MAX_LEXGION_STACK_DEPTH) {
       fprintf(stderr, "thread %d lexgion stack overflow\n", global_thread_num);
       return;
    }
    pinsight_thread_data.lexgion_stack[top].lgp = lgp;
    pinsight_thread_data.lexgion_stack[top].counter = counter;
    pinsight_thread_data.stack_top++;
}

/**
 * pop the lexgion out of the stack and also return the counter if it is requested.
 * @param counter
 * @return
 */
lexgion_t * pop_lexgion(unsigned int * counter) {
    int top = pinsight_thread_data.stack_top;
    lexgion_t * lgp = pinsight_thread_data.lexgion_stack[top].lgp;
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
static lexgion_t *find_lexgion(int class, int type, const void *codeptr_ra, int * index) {
    /* play it safe for dealing with data race */
    if (pinsight_thread_data.recent_lexgion < 0 || pinsight_thread_data.num_lexgions <= 0) return NULL;
    int i;
    lexgion_t * lgp;

    //sanity check for the way of 64-bit uuid is created [codeptr_ra][counter] (32 bits each)
    if ((uint64_t)codeptr_ra >= 0xFFFFFFFF) {
        fprintf(stderr, "FATAL: codeptr_ra (%p) are greater than 2^^32, which is fatal because we "
                "rely on 32-bit codeptr_ra address to create uuid for a region\n", codeptr_ra);
    }
    /* search forward from the most recent one */
    for (i=pinsight_thread_data.recent_lexgion; i<pinsight_thread_data.num_lexgions; i++) {
        if (class == pinsight_thread_data.lexgions[i].class &&
                type == pinsight_thread_data.lexgions[i].type &&
                pinsight_thread_data.lexgions[i].codeptr_ra == codeptr_ra) {
            *index = i;
            lgp = &pinsight_thread_data.lexgions[i];
            return lgp;
        } else if (codeptr_ra != pinsight_thread_data.lexgions[i].codeptr_ra) {
            /* this is where we check whether the uuid approach will work or not */
            if (CODEPTR_RA_4UUID_PREFIX(codeptr_ra) == CODEPTR_RA_4UUID_PREFIX(pinsight_thread_data.lexgions[i].codeptr_ra)) {
                fprintf(stderr, "FATAL: Two different codeptr_ra (%p and %p) are rounded to the same UUID prefix\n",
                codeptr_ra, pinsight_thread_data.lexgions[i].codeptr_ra);
            }
        }
    }
    /* search from 0 to most recent one */
    for (i=0; i<pinsight_thread_data.recent_lexgion; i++) {
        if (class == pinsight_thread_data.lexgions[i].class &&
                pinsight_thread_data.lexgions[i].codeptr_ra == codeptr_ra &&
                type == pinsight_thread_data.lexgions[i].type) {
            *index = i;
            lgp = &pinsight_thread_data.lexgions[i];
            return lgp;
        } else if (codeptr_ra != pinsight_thread_data.lexgions[i].codeptr_ra) {
            /* this is where we check whether the uuid approach will work or not */
            if (CODEPTR_RA_4UUID_PREFIX(codeptr_ra) == CODEPTR_RA_4UUID_PREFIX(pinsight_thread_data.lexgions[i].codeptr_ra)) {
                fprintf(stderr, "FATAL: Two different codeptr_ra (%p and %p) are rounded to the same UUID prefix\n",
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
lexgion_t *lexgion_begin(int class, int type, const void *codeptr_ra) {
    if (pinsight_thread_data.num_lexgions == MAX_NUM_LEXGIONS) {
        fprintf(stderr, "Max number of lex regions (%d) allowed in the source code reached\n",
                MAX_NUM_LEXGIONS);
        return NULL;
    }

    int index;

    lexgion_t *lgp = find_lexgion(class, type, codeptr_ra, &index);
    if (lgp == NULL) {
        index = pinsight_thread_data.num_lexgions;
        pinsight_thread_data.num_lexgions++;
        lgp = &pinsight_thread_data.lexgions[index];
        lgp->codeptr_ra = codeptr_ra;
        //printf("%d: lexgion_begin(%d, %X): first time encountered %X\n", thread_id, i, codeptr_ra, lgp);
        lgp->type = type;
        lgp->class = class;

        /* init counters for number of exes, traces, and sampling */
        lgp->counter = 0;
        lgp->trace_counter = 0;
        lgp->num_exes_after_last_trace = 0;
        lgp->trace_config = retrieve_lexgion_config(codeptr_ra);
    }
    pinsight_thread_data.recent_lexgion = index; /* cache it for future search */

    lgp->num_exes_after_last_trace++;
    lgp->counter++; //counter only increment
    if (lgp->counter >= 0xFFFFFFFF) {
        fprintf(stderr, "FATAL: Trace record overflow, more than 2^^16 traces (%d) would be recorded for lexgion: %p\n", lgp->counter, codeptr_ra);
    }
    push_lexgion(lgp, lgp->counter);

    return lgp;
}

/**
 * mark the end of a lexgion, return the lexgion and the counter of the instance if it is requested
 * @param counter the counter of the lexgion instance that is just ended.
 * @return
 */
lexgion_t *lexgion_end(unsigned int * counter) {
    return pop_lexgion(counter);
}

/**
 * search the lexgion stack to find the topmost lexgion instance in the stack of the specified type. The counter of the
 * lexgion instance is returned via the counter argument
 * @param type
 * @param counter
 * @return
 */
lexgion_t * top_lexgion_type(int class, int type, unsigned int * counter) {
    int top = pinsight_thread_data.stack_top;
    while (top >=0) {
        lexgion_t *lgp = pinsight_thread_data.lexgion_stack[top].lgp;
        if (lgp->class == class && lgp->type == type) {
            if (counter != NULL) *counter = pinsight_thread_data.lexgion_stack[top].counter;
            return lgp;
        }
        top--;
    }
    return NULL;
}

lexgion_t * top_lexgion(unsigned int * counter) {
    int top = pinsight_thread_data.stack_top;
    if (top >=0) {
        lexgion_t *lgp = pinsight_thread_data.lexgion_stack[top].lgp;
        if (counter != NULL) *counter = pinsight_thread_data.lexgion_stack[top].counter;
        return lgp;
    }
    return NULL;
}
