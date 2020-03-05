#include <stdio.h>
#include <stdlib.h>
#include "pinsight.h"

/* these are thread-local storage assuming that OpenMP runtime threads are 1:1 mapped to
 * the system threads (PThread for example). If not, we should implement our own OpenMP
 * thread-local storage (not system TLS).
 */
__thread int global_thread_num = 0;
__thread int omp_thread_num = 0;
__thread const void * parallel_codeptr = NULL;
__thread unsigned int parallel_counter = -1;
__thread const void * task_codeptr = NULL;
__thread unsigned int task_counter = -1;
__thread lexgion_t * ompt_implicit_task = NULL;
__thread pinsight_thread_data_t pinsight_thread_data;
__thread int trace_bit = 0; /* 0 or 1 for enabling trace */

lexgion_trace_config_t lexgion_trace_config[MAX_NUM_LEXGIONS];

static int set_initial_lexgion_trace_config_done = 0;
/**
 * This function is not thread safe, but it seems won't hurt calling by multiple threads
 */
__attribute__ ((constructor)) void set_initial_lexgion_trace_config() {
    if (set_initial_lexgion_trace_config_done) return;

    const char* pinsight_trace_config = getenv("PINSIGHT_TRACE_CONFIG");
    unsigned int NUM_INITIAL_TRACES = DEFAULT_NUM_INITIAL_TRACES;
    unsigned int MAX_NUM_TRACES = DEFAULT_MAX_NUM_TRACES;
    unsigned int TRACE_SAMPLING_RATE = DEFAULT_TRACE_SAMPLING_RATE;
    if (pinsight_trace_config != NULL) {
        printf("PINSIGHT_TRACE_CONFIG: %s\n", pinsight_trace_config);
        sscanf(pinsight_trace_config, "%d:%d:%d", &NUM_INITIAL_TRACES, &MAX_NUM_TRACES, &TRACE_SAMPLING_RATE);
        printf("NUM_INITIAL_TRACES: %u, MAX_NUM_TRACES: %u, TRACE_SAMPLING_RATE: %u\n",
               NUM_INITIAL_TRACES, MAX_NUM_TRACES, TRACE_SAMPLING_RATE);
    }

    int i;
    for (i=0; i<MAX_NUM_LEXGIONS; i++) {
        lexgion_trace_config[i].codeptr_ra = NULL;
        lexgion_trace_config[i].trace_enable = 1;
        lexgion_trace_config[i].max_num_traces = MAX_NUM_TRACES;
        lexgion_trace_config[i].num_initial_traces = NUM_INITIAL_TRACES;
        lexgion_trace_config[i].sample_rate = TRACE_SAMPLING_RATE;
    }

    set_initial_lexgion_trace_config_done = 1;
}

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
                fprintf(stderr, "Two different codeptr_ra (%p and %p) are rounded to the same UUID prefix\n",
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

        /* link with an/existing lexgion_trace_config object */
        int i;
        int done = 0;
        while (!done) {
            int unused_entry = MAX_NUM_LEXGIONS;
            for (i = 0; i < MAX_NUM_LEXGIONS; i++) {
                if (lexgion_trace_config[i].codeptr_ra == codeptr_ra) {/* one already exists, and we donot check class and type */
                    /* trace_config already set before or by others, we just need to link */
                    lgp->trace_config = &lexgion_trace_config[i];
                    done = 1; /* to break the outer while loop */
                    break;
                } else if (lexgion_trace_config[i].codeptr_ra == NULL && unused_entry == MAX_NUM_LEXGIONS) {
                    unused_entry = i;
                }
            }
            if (i == MAX_NUM_LEXGIONS) {
                lexgion_trace_config_t * config = &lexgion_trace_config[unused_entry]; /* we must have an unused entry */
                /* data race here, we must protect updating codeptr by multiple threads, use cas to do it */
                if (config->codeptr_ra == NULL && __sync_bool_compare_and_swap((uint64_t*)&config->codeptr_ra, NULL, codeptr_ra)) {
                    /* a brand new one */
                    lgp->trace_config = config;
                    break;
                } /* else, go back to the loop and check again */
            }
        }
    }
    pinsight_thread_data.recent_lexgion = index; /* cache it for future search */

    lgp->counter++; //counter only increment
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
