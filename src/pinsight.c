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

    pinsight_thread_data.initialized = 1;

    return &pinsight_thread_data;
}

/**
 * push the encounting lexgion instance to the stack
 * @param lgp
 * @param record_id
 */
lexgion_record_t * push_lexgion(lexgion_t * lgp, unsigned int record_id) {
    int top = pinsight_thread_data.stack_top+1;
    if (top == MAX_LEXGION_STACK_DEPTH) {
       fprintf(stderr, "thread %d lexgion stack overflow\n", global_thread_num);
       return NULL;
    }
    lexgion_record_t * record = &pinsight_thread_data.lexgion_stack[top];
    record->lgp = lgp;
    record->record_id = record_id;
    pinsight_thread_data.stack_top = top;

    return record;
}

/**
 * pop the lexgion out of the stack and also return the record_id if it is requested.
 * @param record_id
 * @return
 */
lexgion_t * pop_lexgion(unsigned int * record_id) {
    int top = pinsight_thread_data.stack_top;
    lexgion_t * lgp = pinsight_thread_data.lexgion_stack[top].lgp;
    if (record_id != NULL) *record_id = pinsight_thread_data.lexgion_stack[top].record_id;
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

    /* search forward from the most recent one */
    for (i=pinsight_thread_data.recent_lexgion; i<pinsight_thread_data.num_lexgions; i++) {
        if (class == pinsight_thread_data.lexgions[i].class &&
                type == pinsight_thread_data.lexgions[i].type &&
                pinsight_thread_data.lexgions[i].codeptr_ra == codeptr_ra) {
            *index = i;
            lgp = &pinsight_thread_data.lexgions[i];
            return lgp;
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
        }
    }
    return NULL;
}
/**
 * This is a thread-specific call
 *
 */
lexgion_record_t *lexgion_begin(int class, int type, const void *codeptr_ra) {
    if (pinsight_thread_data.num_lexgions == MAX_NUM_LEXGIONS) {
        fprintf(stderr, "FATAL: Max number of lexgions (%d) allowed in the source code reached, cannot continue\n",
                MAX_NUM_LEXGIONS);
        return NULL;
    }

    int index;

    lexgion_t *lgp = find_lexgion(class, type, codeptr_ra, &index);
    if (lgp == NULL) {
        index = pinsight_thread_data.num_lexgions;
        lgp = &pinsight_thread_data.lexgions[index];
        pinsight_thread_data.num_lexgions++;
        lgp->codeptr_ra = codeptr_ra;
        //printf("%d: lexgion_begin(%d, %X): first time encountered %X\n", thread_id, i, codeptr_ra, lgp);
        lgp->type = type;
        lgp->class = class;

        /* init counters for number of exes, traces, and sampling */
        lgp->counter = 0;
        lgp->trace_counter = 0;
        lgp->num_exes_after_last_trace = 0;
        lgp->trace_config = retrieve_lexgion_trace_config(codeptr_ra);
    }
    pinsight_thread_data.recent_lexgion = index; /* cache it for future search */

    lgp->num_exes_after_last_trace++;
    lgp->counter++; //counter only increment
    if (lgp->counter >= 0xFFFF) {
        //fprintf(stderr, "FATAL: Trace record overflow, more than 2^^16 traces (%d) would be recorded for lexgion: %p\n", lgp->counter, codeptr_ra);
    }

    return push_lexgion(lgp, lgp->counter);
}

/**
 * mark the end of a lexgion, return the lexgion and the record_id of the instance if it is requested
 * @param record_id: record_id of the lexgion instance that is just ended.
 * @return
 */
lexgion_t *lexgion_end(unsigned int * record_id) {
    return pop_lexgion(record_id);
}

/**
 * search the lexgion stack to find the topmost lexgion record in the stack of the specified type.
 * @param type
 * @return
 */
lexgion_record_t * top_lexgion_type(int class, int type) {
    int top = pinsight_thread_data.stack_top;
    while (top >=0) {
    	lexgion_record_t *record = &pinsight_thread_data.lexgion_stack[top];
    	lexgion_t * lgp = record->lgp;
        if (lgp->class == class && lgp->type == type) {
            return record;
        }
        top--;
    }
    return NULL;
}

lexgion_record_t * top_lexgion() {
    int top = pinsight_thread_data.stack_top;
    if (top >=0) {
        return &pinsight_thread_data.lexgion_stack[top];
    }
    return NULL;
}
