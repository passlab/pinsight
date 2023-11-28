#include <stdio.h>
#include <inttypes.h>
#include <omp.h>
#include <omp-tools.h>
#include <execinfo.h>
#ifdef OMPT_USE_LIBUNWIND
#define UNW_LOCAL_ONLY
#include <libunwind.h>
#endif
#include "lexgion_trace_config.h"
#include "pinsight.h"

// --------------------------------------------------------
// Environment config variable names and default values.

#define PINSIGHT_DEBUG_ENABLE "PINSIGHT_DEBUG_ENABLE"
#define PINSIGHT_DEBUG_ENABLE_DEFAULT 0

// Configuration settings.
static int debug_on;

#ifdef PINSIGHT_ENERGY
#include "rapl.h"
// --------------------------------------------------------
// RAPL package values.
static long long package_energy[MAX_PACKAGES];
#endif

#define LTTNG_UST_TRACEPOINT_CREATE_PROBES
#define LTTNG_UST_TRACEPOINT_DEFINE
#include "ompt_lttng_ust_tracepoint.h"

extern int __kmpc_global_thread_num(void *);
extern int __kmpc_global_num_threads(void *);

#define get_global_thread_num() __kmpc_global_thread_num(NULL)
#define get_global_num_threads() __kmpc_global_num_threads(NULL)

/* these are thread-local storage assuming that OpenMP runtime threads are 1:1 mapped to
 * the system threads (PThread for example). If not, we should implement our own OpenMP
 * thread-local storage (not system TLS).
 */
__thread lexgion_record_t * enclosing_parallel_lexgion_record = NULL;
__thread lexgion_record_t * enclosing_task_lexgion_record = NULL;
__thread const void * parallel_codeptr = NULL;
__thread unsigned int parallel_record_id = -1;
__thread const void * task_codeptr = NULL;
__thread unsigned int task_record_id = -1;

__thread int global_thread_num = 0;
__thread int omp_team_num = 0;
__thread int omp_thread_num = 0;

static const char* ompt_thread_type_t_values[] = {
  NULL,
  "ompt_thread_initial",
  "ompt_thread_worker",
  "ompt_thread_other"
};

static const char* ompt_task_type_t_values[] = {
  NULL,
  "ompt_task_initial",
  "ompt_task_implicit",
  "ompt_task_explicit",
  "ompt_task_target"
};

static const char* ompt_task_status_t_values[] = {
  NULL,
  "ompt_task_complete",
  "ompt_task_yield",
  "ompt_task_cancel",
  "ompt_task_others"
};
static const char* ompt_cancel_flag_t_values[] = {
  "ompt_cancel_parallel",
  "ompt_cancel_sections",
  "ompt_cancel_loop",
  "ompt_cancel_taskgroup",
  "ompt_cancel_activated",
  "ompt_cancel_detected",
  "ompt_cancel_discarded_task"
};

static ompt_set_callback_t ompt_set_callback;
static ompt_get_task_info_t ompt_get_task_info;
static ompt_get_thread_data_t ompt_get_thread_data;
static ompt_get_parallel_info_t ompt_get_parallel_info;
static ompt_get_unique_id_t ompt_get_unique_id;
static ompt_get_num_places_t ompt_get_num_places;
static ompt_get_place_proc_ids_t ompt_get_place_proc_ids;
static ompt_get_place_num_t ompt_get_place_num;
static ompt_get_partition_place_nums_t ompt_get_partition_place_nums;
static ompt_get_proc_id_t ompt_get_proc_id;

static void print_ids(int level)
{
  ompt_frame_t* frame ;
  ompt_data_t* parallel_data;
  ompt_data_t* task_data;
//  int exists_parallel = ompt_get_parallel_info(level, &parallel_data, NULL);
  int exists_task = ompt_get_task_info(level, NULL, &task_data, &frame, &parallel_data, NULL);
  if (frame)
  {
    if (debug_on) {
      printf("%" PRIu64 ": task level %d: parallel_id=%" PRIu64 ", task_id=%" PRIu64 ", exit_frame=%p, enter_frame=%p\n", ompt_get_thread_data()->value, level, exists_task ? parallel_data->value : 0, exists_task ? task_data->value : 0, frame->exit_frame.ptr, frame->enter_frame.ptr);
    }
//    printf("%" PRIu64 ": parallel level %d: parallel_id=%" PRIu64 ", task_id=%" PRIu64 ", exit_frame=%p, reenter_frame=%p\n", ompt_get_thread_data()->value, level, exists_parallel ? parallel_data->value : 0, exists_task ? task_data->value : 0, frame->exit_frame.ptr, frame->enter_frame.ptr);
  }
  else {
    if (debug_on) {
      printf("%" PRIu64 ": task level %d: parallel_id=%" PRIu64 ", task_id=%" PRIu64 ", frame=%p\n", ompt_get_thread_data()->value, level, exists_task ? parallel_data->value : 0, exists_task ? task_data->value : 0, frame);
    }
  }
}

/*
#define print_frame(level)\
do {\
  unw_cursor_t cursor;\
  unw_context_t uc;\
  unw_word_t fp;\
  unw_getcontext(&uc);\
  unw_init_local(&cursor, &uc);\
  int tmp_level = level;\
  unw_get_reg(&cursor, UNW_REG_SP, &fp);\
  if (debug_on) {\
    printf("callback %p\n", (void*)fp);\
  }\
  while (tmp_level > 0 && unw_step(&cursor) > 0)\
  {\
    unw_get_reg(&cursor, UNW_REG_SP, &fp);\
    if (debug_on) {\
      printf("callback %p\n", (void*)fp);\
    }\
    tmp_level--;\
  }\
  if(tmp_level == 0)\
    if (debug_on) {\
      printf("%" PRIu64 ": __builtin_frame_address(%d)=%p\n", ompt_get_thread_data()->value, level, (void*)fp);\
    }\
  elsei {\
    if (debug_on) {\
      printf("%" PRIu64 ": __builtin_frame_address(%d)=%p\n", ompt_get_thread_data()->value, level, NULL);\
    }\
  }\
} while(0)
*/

#define print_frame(level)\
do {\
  if (debug_on) {\
    printf("%" PRIu64 ": __builtin_frame_address(%d)=%p\n", ompt_get_thread_data()->value, level, __builtin_frame_address(level));\
  }\
} while(0)

static void print_current_address()
{
    int real_level = 2;
    void *array[real_level];
    size_t size;
    void *address;
  
    size = backtrace (array, real_level);
    if(size == real_level)
      address = array[real_level-1]-5;
    else
      address = NULL;

  if (debug_on) {
    printf("%" PRIu64 ": current_address=%p\n", ompt_get_thread_data()->value, address);
  }
}

/**
 * We manually set the codeptr_ra as 0x00000FFFFFF for the initial thread-local parallel_codeptr
 * and task_codeptr variable for each thread. A lexgion is created for this codeptr address with thread_begin event.
 * This lexgion will be at the bottom of the lexgion stack of each thread.
 *
 * For the initial task lexgion of the whole program, which is created in the task_create callback, we could
 * set the codeptr_ra as 0x00000FFFFFFFF, so far, we do not know what it will be used for. But since it is
 * part of the standard and the OMPT implements this, we can add that lexgion in case it become useful for
 * the future. Again, it is only one instance of that.
 *
 * We do not have lexgion for the implicit parallel region.
 * @param thread_type
 * @param thread_data
 */
#define INITIAL_PARALLEL_CODEPTR 0xFFFFFFFFFFFF
#define UNKNOWN_END_CODEPTR      0x100000000000
#define NULL_DEFAULT_CODEPTR     0x200000000000
static void
on_ompt_callback_thread_begin(
        ompt_thread_t thread_type,
        ompt_data_t *thread_data) {
    thread_data->ptr = (void*)init_thread_data(get_global_thread_num() /*, thread_type */);
#ifdef PINSIGHT_ENERGY
    rapl_sysfs_read_packages(package_energy); // Read package energy counters.
#endif
    lttng_ust_tracepoint(ompt_pinsight_lttng_ust, thread_begin, (short)thread_type ENERGY_LTTNG_UST_TRACEPOINT_CALL_ARGS);

    if (thread_type == ompt_thread_initial) { //For initial thread, we need to setup the initial parallel region and initial implicit task
        pinsight_thread_data.initial_thread = 1;

        //Setup the initial parallel region, and this could be a call to the callback, but we will elaborate it here
        //on_ompt_callback_parallel_begin(NULL, NULL, &pinsight_thread_data.lexgion_stack[0], 1, ompt_parallel_team || ompt_parallel_invoker_runtime, INITIAL_PARALLEL_CODEPTR);
        enclosing_parallel_lexgion_record = lexgion_begin(OPENMP_LEXGION, ompt_callback_parallel_begin, (void*)INITIAL_PARALLEL_CODEPTR);
        //printf("thread_begin: initial parallel lexgion: %p\n", enclosing_parallel_lexgion_record->lgp);
        parallel_codeptr = (void*)INITIAL_PARALLEL_CODEPTR;
        parallel_record_id = enclosing_parallel_lexgion_record->record_id;
        lttng_ust_tracepoint(ompt_pinsight_lttng_ust, parallel_begin, 1, ompt_parallel_team || ompt_parallel_invoker_runtime, NULL ENERGY_LTTNG_UST_TRACEPOINT_CALL_ARGS);

        omp_thread_num = 0;

        //NO NEED THIS ANYMORE since initial implicit task is setup by the callback_implicit_task
#if 0
        //Setup the initial implicit task, not sure whether this can be a call to the on_ompt_callback_implicit_task
        enclosing_task_lexgion_record = lexgion_begin(OPENMP_LEXGION, ompt_callback_implicit_task, (void*)INITIAL_PARALLEL_CODEPTR);
        debugging_initial_implicit_task = enclosing_task_lexgion_record->lgp;
        task_codeptr = (void*)INITIAL_PARALLEL_CODEPTR;
        task_record_id = enclosing_task_lexgion_record->record_id;
        printf("thread_begin: initial implicit task lexgion: %p, record_id: %d\n", enclosing_task_lexgion_record->lgp, task_record_id);
#ifdef PINSIGHT_BACKTRACE
        retrieve_backtrace();
#endif
        lttng_ust_tracepoint(ompt_pinsight_lttng_ust, implicit_task_begin, 1 ENERGY_LTTNG_UST_TRACEPOINT_CALL_ARGS);
#endif
    } else {
        pinsight_thread_data.initial_thread = 0;
    }

    //This is the codeptr for the first lexgion of each thread
    //lexgion_t * lgp = lexgion_begin(OPENMP_LEXGION, ompt_callback_thread_begin, codeptr_ra);
}

#define PRINTF_LEXGION_HEADER printf("#\t\tcodeptr\t\t\ttype\tcount\t\ttrace count(first-last)\n")
#define PRINTF_LEXGION_INFO(i, lgp) printf("%d\t%p-%p\t%d\t%d\t\t%d(%d-%d)\n", i, lgp->codeptr_ra, lgp->end_codeptr_ra == NULL ? (void*)NULL_DEFAULT_CODEPTR : lgp->end_codeptr_ra, lgp->type, lgp->counter, lgp->trace_counter, lgp->first_trace_num, lgp->last_trace_num)

#define PRINT_LEXGION_SUMMARY 1

static void
on_ompt_callback_thread_end(
        ompt_data_t *thread_data)
{
    if (pinsight_thread_data.initial_thread) { //we should pop up initial implicit task and initial parallel region that are set up by the thread_begin
        unsigned int record_id;
        lexgion_t * lgp;

        //NO NEED THIS ANYMORE since initial implicit task is processed by the callback_implicit_task
#if 0
		lgp = lexgion_end(&record_id); //pop up initial implicit task
        lgp->end_codeptr_ra = (void*)INITIAL_PARALLEL_CODEPTR;
        assert(lgp->codeptr_ra == (void*)INITIAL_PARALLEL_CODEPTR);
        printf("thread_end: initial implicit task lexgion: %p\n", lgp);
        printf("counter: %d, lgp->counter: %d\n", record_id, lgp->record_id);
        //assert(counter == lgp->counter);
        lttng_ust_tracepoint(ompt_pinsight_lttng_ust, implicit_task_end, 1 ENERGY_LTTNG_UST_TRACEPOINT_CALL_ARGS);
        lexgion_post_trace_update(lgp);
#endif

        lgp = lexgion_end(&record_id); //pop up initial parallel region
        lgp->end_codeptr_ra = (void*)UNKNOWN_END_CODEPTR;
        assert(lgp->codeptr_ra == (void*)INITIAL_PARALLEL_CODEPTR);
        //printf("thread_end: initial parallel lexgion: %p\n", lgp);
        assert(record_id == lgp->counter == 1); //both should be 1, since it is only recorded once

#ifdef PINSIGHT_BACKTRACE
        retrieve_backtrace();
#endif
        lttng_ust_tracepoint(ompt_pinsight_lttng_ust, parallel_end, ompt_parallel_team || ompt_parallel_invoker_runtime ENERGY_LTTNG_UST_TRACEPOINT_CALL_ARGS);
        lexgion_post_trace_update(lgp);
    } else {
    }
#ifdef PINSIGHT_ENERGY
    rapl_sysfs_read_packages(package_energy); // Read package energy counters.
#endif
    lttng_ust_tracepoint(ompt_pinsight_lttng_ust, thread_end, 0 ENERGY_LTTNG_UST_TRACEPOINT_CALL_ARGS);

#ifdef PRINT_LEXGION_SUMMARY
  //print out lexgion summary */
  if (global_thread_num == 0) {
    printf("======================================================================================\n");
    printf("======================================================================================\n");
    printf("=========================== PINSIGHT TRACING REPORT ==================================\n");
    printf("======================================================================================\n");
    printf("Lexgion report from thread 0: total %d lexgions\n", pinsight_thread_data.num_lexgions);
    PRINTF_LEXGION_HEADER;
    int i;
    int count=1;
    for (i=0; i<pinsight_thread_data.num_lexgions; i++) {
        lexgion_t* lgp = &pinsight_thread_data.lexgions[i];
        PRINTF_LEXGION_INFO(count++, lgp);
    }

    printf("----------------------------------------------------------------------------------------\n");
    printf("parallel lexgions (type %d) from thread 0\n", ompt_callback_parallel_begin);
    PRINTF_LEXGION_HEADER;
    count = 1;
    for (i=0; i<pinsight_thread_data.num_lexgions; i++) {
      lexgion_t* lgp = &pinsight_thread_data.lexgions[i];
      if (lgp->type == ompt_callback_parallel_begin)
          PRINTF_LEXGION_INFO(count++, lgp);
    }

    printf("----------------------------------------------------------------------------------------\n");
    printf("sync lexgions (type %d) from thread 0\n", ompt_callback_sync_region);
    PRINTF_LEXGION_HEADER;
    count = 1;
    for (i=0; i<pinsight_thread_data.num_lexgions; i++) {
      lexgion_t* lgp = &pinsight_thread_data.lexgions[i];
      if (lgp->type == ompt_callback_sync_region)
          PRINTF_LEXGION_INFO(count++, lgp);
    }

    printf("----------------------------------------------------------------------------------------\n");
    printf("work lexgions (type %d) from thread 0\n", ompt_callback_work);
    PRINTF_LEXGION_HEADER;
    count = 1;
    for (i=0; i<pinsight_thread_data.num_lexgions; i++) {
      lexgion_t* lgp = &pinsight_thread_data.lexgions[i];
      if (lgp->type == ompt_callback_work)
          PRINTF_LEXGION_INFO(count++, lgp);
    }

    printf("----------------------------------------------------------------------------------------\n");
    printf("masked lexgions (type %d) from thread 0\n", ompt_callback_masked);
    PRINTF_LEXGION_HEADER;
    count = 1;
    for (i=0; i<pinsight_thread_data.num_lexgions; i++) {
      lexgion_t* lgp = &pinsight_thread_data.lexgions[i];
      if (lgp->type == ompt_callback_masked)
          PRINTF_LEXGION_INFO(count++, lgp);
    }
    printf("==========================================================================================\n");

  }
#endif
}

static void
on_ompt_callback_parallel_begin(
        ompt_data_t *parent_task,         /* data of encountering task           */
        const ompt_frame_t *parent_task_frame,  /* frame data of encountering task     */
        ompt_data_t* parallel_data,
        unsigned int requested_team_size,
        int flag,
        const void *codeptr_ra)
{
	//void * extracted_codeptr_ra = __builtin_extract_return_addr(codeptr_ra);
	//printf("codeptr: %x, extracted: %x\n", codeptr_ra, extracted_codeptr_ra);
//  parallel_data->value = ompt_get_unique_id();
//  printf("parallel_begin: codeptr_ra: 0x%" PRIx64 "\n", codeptr_ra);
  enclosing_parallel_lexgion_record = lexgion_begin(OPENMP_LEXGION, ompt_callback_parallel_begin, codeptr_ra);
  //This parallel_data->ptr will be passed to the callbacks of implicit tasks,
  parallel_data->ptr = enclosing_parallel_lexgion_record;
  //lgp->num_exes_after_last_trace ++;

  /* Set up thread local for tracing, though the implicit task will do them */
  parallel_codeptr = codeptr_ra;
  parallel_record_id = enclosing_parallel_lexgion_record->record_id;
  omp_thread_num = 0;
  lexgion_t * lgp = enclosing_parallel_lexgion_record->lgp;
  lexgion_set_trace_bit(lgp);
  if (trace_bit) {
#ifdef PINSIGHT_ENERGY
    if (global_thread_num == 0) {
      rapl_sysfs_read_packages(package_energy); // Read package energy counters.
    }
#endif
    void * enter_frame_ptr = (parent_task_frame == NULL) ? NULL : parent_task_frame->enter_frame.ptr;
#ifdef PINSIGHT_BACKTRACE
    retrieve_backtrace();
#endif
    lttng_ust_tracepoint(ompt_pinsight_lttng_ust, parallel_begin, requested_team_size, flag, enter_frame_ptr
               ENERGY_LTTNG_UST_TRACEPOINT_CALL_ARGS);
  }
}

static void
on_ompt_callback_parallel_end(
        ompt_data_t *parallel_data,
        ompt_data_t *parent_task,
        int flag,
        const void *codeptr_ra)
{
  lexgion_t * lgp = lexgion_end(NULL); //pop up the current parallel lexgion record
  assert(parallel_data->ptr == enclosing_parallel_lexgion_record);
  lgp->end_codeptr_ra = codeptr_ra;
  //turn off this assertation. LLVM openmp runtime has a bug that for serialized parallel region, codeptr_ra for parallel_end callback is NULL 
  //assert (lgp->codeptr_ra == codeptr_ra); /* for parallel region and parallel_end event */
  //printf("parallel_end: begin codeptr_ra: %x, end codeptr_ra: %x\n", lgp->codeptr_ra, codeptr_ra);
  if (trace_bit) {
#ifdef PINSIGHT_ENERGY
    if (global_thread_num == 0) {
      rapl_sysfs_read_packages(package_energy); // Read package energy counters.
    }
#endif

#ifdef PINSIGHT_BACKTRACE
    retrieve_backtrace();
#endif
    lttng_ust_tracepoint(ompt_pinsight_lttng_ust, parallel_end, flag ENERGY_LTTNG_UST_TRACEPOINT_CALL_ARGS);
    lexgion_post_trace_update(lgp);
  }
  /* find the topmost parallel lexgion in the lexgion record stack including nest parallel-parallel regions,
   * but not considering the explicit task-parallel situation) */
  enclosing_task_lexgion_record = top_lexgion_type(OPENMP_LEXGION, ompt_callback_implicit_task); /* not considering task->parallel nested */
  assert(enclosing_task_lexgion_record == parent_task->ptr);
  task_codeptr = enclosing_task_lexgion_record->lgp->codeptr_ra;
  task_record_id = enclosing_task_lexgion_record->record_id;
  enclosing_parallel_lexgion_record = top_lexgion_type(OPENMP_LEXGION, ompt_callback_parallel_begin);
  parallel_codeptr = enclosing_parallel_lexgion_record->lgp->codeptr_ra;
  parallel_record_id = enclosing_parallel_lexgion_record->record_id;
  omp_thread_num = 0;

  /* For explicit task-parallel nested, the ompt_task_create should be set, task_codeptr and task_record_id
  * TODO: we need to store and restore the omp_thread_num, task_codeptr, task_record_id in the nested parallel situation */
}

/**
 * only parallel, teams and initial implicit tasks can trigger this callback
 */
static void
on_ompt_callback_implicit_task(
        ompt_scope_endpoint_t endpoint,
        ompt_data_t *parallel_data,
        ompt_data_t *task_data,
        unsigned int team_size,
        unsigned int thread_num,
        int flags)
{
  switch(endpoint)
  {
    case ompt_scope_begin: {
      if (flags & ompt_task_initial) { //For the initial parallel region, there is no parallel_begin event, thus we set the parallel_data here
          parallel_data->ptr = (void*)enclosing_parallel_lexgion_record;
      }

      //These three setup are redundant for the main thread since they are already setup by the parallel_begin. But they are needed for worker threads
      enclosing_parallel_lexgion_record = parallel_data->ptr;
      parallel_codeptr = enclosing_parallel_lexgion_record->lgp->codeptr_ra;
      parallel_record_id = enclosing_parallel_lexgion_record->record_id;

      omp_thread_num = thread_num;
      /// Here a new lexgion with the same codeptr as the parallel region is created, but this lexgion has implicit_task type
      enclosing_task_lexgion_record = lexgion_begin(OPENMP_LEXGION, ompt_callback_implicit_task, parallel_codeptr);
      task_data->ptr = (void*)enclosing_task_lexgion_record;
      task_codeptr = parallel_codeptr;
      task_record_id = enclosing_task_lexgion_record->record_id;
      lexgion_t * lgp = enclosing_task_lexgion_record->lgp;
      lexgion_set_trace_bit(lgp);
      if (trace_bit) {
#ifdef PINSIGHT_ENERGY
        if (global_thread_num == 0) {
          rapl_sysfs_read_packages(package_energy); // Read package energy counters.
        }
#endif
#ifdef PINSIGHT_BACKTRACE
        retrieve_backtrace();
#endif
        lttng_ust_tracepoint(ompt_pinsight_lttng_ust, implicit_task_begin, team_size ENERGY_LTTNG_UST_TRACEPOINT_CALL_ARGS);
      }
      break;
    }
    case ompt_scope_end: {
      //parallel_data is NULL here
      if (flags & ompt_task_initial) { //nothing special
      }
      lexgion_t * lgp = lexgion_end(NULL);
      lgp->end_codeptr_ra = (void*)UNKNOWN_END_CODEPTR; //Sadly, it is unknow at this point since parallel_end happens after this event callback
      if (trace_bit) {
#ifdef PINSIGHT_ENERGY
        if (global_thread_num == 0) {
          rapl_sysfs_read_packages(package_energy); // Read package energy counters.
        }
#endif
#ifdef PINSIGHT_BACKTRACE
        retrieve_backtrace();
#endif
        lttng_ust_tracepoint(ompt_pinsight_lttng_ust, implicit_task_end, team_size ENERGY_LTTNG_UST_TRACEPOINT_CALL_ARGS);
        lexgion_post_trace_update(lgp);
      }
        /* for the nested situation (parallel-parallel, and task-parallel), popping back to the state of the enclosing
         * is set by the parallel_end callback for the main thread. For other thread, we might not need this at all since they
         * will be setup when this thread is used later on
         */
      break;
    }
  }
}

static void
on_ompt_callback_work(
        ompt_work_t wstype,
        ompt_scope_endpoint_t endpoint,
        ompt_data_t *parallel_data,
        ompt_data_t *task_data,
        uint64_t count,
        const void *codeptr_ra)
{
  switch(endpoint)
  {
    case ompt_scope_begin:
      if (codeptr_ra != parallel_codeptr && codeptr_ra != task_codeptr) {
        /* safety check for combined construct, such as parallel_for */
        lexgion_record_t * record = lexgion_begin(OPENMP_LEXGION, ompt_callback_work, codeptr_ra);
        lexgion_t *lgp = record->lgp;
        lexgion_set_trace_bit(lgp);

        if (trace_bit) {
#ifdef PINSIGHT_ENERGY
          if (global_thread_num == 0) {
            rapl_sysfs_read_packages(package_energy); // Read package energy counters.
          }
#endif
#ifdef PINSIGHT_BACKTRACE
          retrieve_backtrace();
#endif
          lttng_ust_tracepoint(ompt_pinsight_lttng_ust, work_begin, (short) wstype, codeptr_ra, (void *) 0x000000, record->record_id,
                     count ENERGY_LTTNG_UST_TRACEPOINT_CALL_ARGS);
        }
      } else {
        fprintf(stderr, "The work_scope_begin lexgion codeptr is the same as enclosing parallel or task codeptr, "
                "something wrong. %p", codeptr_ra);
      }
      switch(wstype) {
         case ompt_work_loop:
           break;
         case ompt_work_sections:
           break;
         case ompt_work_single_executor:
           break;
         case ompt_work_single_other:
           break;
         case ompt_work_workshare:
           break;
         case ompt_work_distribute:
           break;
         case ompt_work_taskloop:
           break;
      }
      break;
    case ompt_scope_end:
      /* at this point, codeptr_ra is the address of the end of scope. We use the address at the beginning
       * of the lexgion as index, thus here we need to retrieve back the beginning address */
      ;
      unsigned int record_id;
      lexgion_t *lgp = lexgion_end(&record_id);
      lgp->end_codeptr_ra = codeptr_ra;
      if (lgp->codeptr_ra != parallel_codeptr && lgp->codeptr_ra != task_codeptr) {/* safety check */
        if (trace_bit) {
#ifdef PINSIGHT_ENERGY
          if (global_thread_num == 0) {
            rapl_sysfs_read_packages(package_energy); // Read package energy counters.
          }
#endif
#ifdef PINSIGHT_BACKTRACE
          retrieve_backtrace();
#endif
          lttng_ust_tracepoint(ompt_pinsight_lttng_ust, work_end, (short) wstype, lgp->codeptr_ra, codeptr_ra, record_id,
                     count ENERGY_LTTNG_UST_TRACEPOINT_CALL_ARGS);
          lexgion_post_trace_update(lgp);
        }
      } else {
        fprintf(stderr, "The work_scope_end lexgion codeptr is the same as enclosing parallel or task codeptr, "
                "something wrong. %p", codeptr_ra);
      }
      break;
  }
}

static void
on_ompt_callback_masked(
        ompt_scope_endpoint_t endpoint,
        ompt_data_t *parallel_data,
        ompt_data_t *task_data,
        const void *codeptr_ra)
{
  switch(endpoint)
  {
    case ompt_scope_begin:
      ;lexgion_record_t * record = lexgion_begin(OPENMP_LEXGION, ompt_callback_masked, codeptr_ra);
      lexgion_t *lgp = record->lgp;
      lexgion_set_trace_bit(lgp);
      if (trace_bit) {
#ifdef PINSIGHT_ENERGY
        if (global_thread_num == 0) {
          rapl_sysfs_read_packages(package_energy); // Read package energy counters.
        }
#endif
#ifdef PINSIGHT_BACKTRACE
        retrieve_backtrace();
#endif
        lttng_ust_tracepoint(ompt_pinsight_lttng_ust, masked_begin, codeptr_ra, (void *) 0x000000, record->record_id
                   ENERGY_LTTNG_UST_TRACEPOINT_CALL_ARGS);
      }
      break;
    case ompt_scope_end:
      ;
      unsigned int record_id;
      lgp = lexgion_end(&record_id);
      lgp->end_codeptr_ra = codeptr_ra;

      if (trace_bit) {
#ifdef PINSIGHT_ENERGY
        if (global_thread_num == 0) {
          rapl_sysfs_read_packages(package_energy); // Read package energy counters.
        }
#endif
#ifdef PINSIGHT_BACKTRACE
        retrieve_backtrace();
#endif
        lttng_ust_tracepoint(ompt_pinsight_lttng_ust, masked_end, lgp->codeptr_ra, codeptr_ra, record_id ENERGY_LTTNG_UST_TRACEPOINT_CALL_ARGS);
        lexgion_post_trace_update(lgp);
      }
      break;
  }
}

/**
 * In this callback, we will separate parallel_join sync from barrier/taskwait/taskgroup sync
 *
 * @param kind
 * @param endpoint
 * @param parallel_data
 * @param task_data
 * @param codeptr_ra
 */
static void
on_ompt_callback_sync_region(
        ompt_sync_region_t kind,
        ompt_scope_endpoint_t endpoint,
        ompt_data_t *parallel_data,
        ompt_data_t *task_data,
        const void *codeptr_ra) {
  unsigned int record_id = 0;
  switch(endpoint) {
    case ompt_scope_begin: {
        switch (kind) {
#if COMPLETE_OPENMPRUNTIME_IMPLEMENTATION_4_BARRIER //until we have a runtime that fully support this as defined in the standard
            case ompt_sync_region_barrier_implicit_parallel:
            case ompt_sync_region_barrier_teams:
            	printf("sync_region: codeptr: %p, parallel_code_ptr: %p, thread num: %d, trace_bit: %d\n", codeptr_ra, parallel_codeptr, global_thread_num, trace_bit);
                if (codeptr_ra == NULL || parallel_codeptr == codeptr_ra) {
                    /* this is the join barrier for the parallel region: if codeptr_ra == NULL: non-master thread;
                     * if parallel_lgp->codeptr_ra == codeptr_ra: master thread */
                    if (trace_bit) {
#ifdef PINSIGHT_ENERGY
                        if (global_thread_num == 0) rapl_sysfs_read_packages(package_energy); // Read package energy counters.
#endif
#ifdef PINSIGHT_BACKTRACE
                        retrieve_backtrace();
#endif
                        lttng_ust_tracepoint(ompt_pinsight_lttng_ust, parallel_join_sync_begin, 0 ENERGY_LTTNG_UST_TRACEPOINT_CALL_ARGS);
                    }
                }
                break;
            case ompt_sync_region_barrier_explicit: //barrier (explicit)
            	printf("sync_region: codeptr: %p, parallel_code_ptr: %p, thread num: %d, trace_bit: %d\n", codeptr_ra, parallel_codeptr, global_thread_num, trace_bit);
                ;lexgion_record_t * barrier_record = lexgion_begin(OPENMP_LEXGION, ompt_callback_sync_region, codeptr_ra);
                record_id = barrier_record->record_id;
                lexgion_t *lgp = barrier_record->lgp;
                lexgion_set_trace_bit(lgp);
                if (trace_bit) {
#ifdef PINSIGHT_ENERGY
                    if (global_thread_num == 0) rapl_sysfs_read_packages(package_energy); // Read package energy counters.
#endif
#ifdef PINSIGHT_BACKTRACE
                    retrieve_backtrace();
#endif
                    lttng_ust_tracepoint(ompt_pinsight_lttng_ust, barrier_explicit_sync_begin, (unsigned short) kind, codeptr_ra, record_id
                                   ENERGY_LTTNG_UST_TRACEPOINT_CALL_ARGS);
                }
                break;
            case ompt_sync_region_barrier_implementation:
            case ompt_sync_region_barrier_implicit_workshare:
            	printf("sync_region: codeptr: %p, parallel_code_ptr: %p, thread num: %d, trace_bit: %d\n", codeptr_ra, parallel_codeptr, global_thread_num, trace_bit);
                /* implicit barrier in worksharing, single, sections, and explicit barrier */
                /* each thread will have a lexgion object for the same lexgion */
                if (trace_bit) {
#ifdef PINSIGHT_ENERGY
                    if (global_thread_num == 0) rapl_sysfs_read_packages(package_energy); // Read package energy counters.
#endif
#ifdef PINSIGHT_BACKTRACE
                    retrieve_backtrace();
#endif
                    lttng_ust_tracepoint(ompt_pinsight_lttng_ust, barrier_implicit_sync_begin, (unsigned short) kind, codeptr_ra
                                   ENERGY_LTTNG_UST_TRACEPOINT_CALL_ARGS);
                }
                break;
#else  //LLVM OpenMP runtime does not fully implement the different kinds of barrier, thus we use the following workaround
            case ompt_sync_region_barrier: //barrier (implicit or explicit)
            case ompt_sync_region_barrier_explicit:
            case ompt_sync_region_barrier_implicit: //for join barrier and implicit barrier such as at the end of a for loop
            case ompt_sync_region_barrier_implementation:
            case ompt_sync_region_barrier_implicit_workshare:
            case ompt_sync_region_barrier_implicit_parallel:
            case ompt_sync_region_barrier_teams:
                if (codeptr_ra == NULL || parallel_codeptr == codeptr_ra) {
                      /* this is the join barrier for the parallel region: if codeptr_ra == NULL: non-master thread;
                       * if parallel_lgp->codeptr_ra == codeptr_ra: master thread */
                      if (trace_bit) {
  #ifdef PINSIGHT_ENERGY
                          if (global_thread_num == 0) rapl_sysfs_read_packages(package_energy); // Read package energy counters.
  #endif
#ifdef PINSIGHT_BACKTRACE
                          retrieve_backtrace();
#endif
                          lttng_ust_tracepoint(ompt_pinsight_lttng_ust, parallel_join_sync_begin, 0 ENERGY_LTTNG_UST_TRACEPOINT_CALL_ARGS);
                      }
                  } else {
                      /* implicit barrier in worksharing, single, sections, and explicit barrier */
                      /* each thread will have a lexgion object for the same lexgion */
                      ;lexgion_record_t * barrier_record = lexgion_begin(OPENMP_LEXGION, ompt_callback_sync_region, codeptr_ra);
                      record_id = barrier_record->record_id;
                      lexgion_t *lgp = barrier_record->lgp;
                      lexgion_set_trace_bit(lgp);
                      if (trace_bit) {
  #ifdef PINSIGHT_ENERGY
                          if (global_thread_num == 0) rapl_sysfs_read_packages(package_energy); // Read package energy counters.
  #endif
#ifdef PINSIGHT_BACKTRACE
                          retrieve_backtrace();
#endif
                          lttng_ust_tracepoint(ompt_pinsight_lttng_ust, barrier_implicit_sync_begin, (unsigned short) kind, codeptr_ra
                                     ENERGY_LTTNG_UST_TRACEPOINT_CALL_ARGS);
                      }
                  }
                  break;
#endif
            case ompt_sync_region_taskwait:
            case ompt_sync_region_reduction:
            case ompt_sync_region_taskgroup:
                break;
        }
        break;
    }
    case ompt_scope_end: {
        switch(kind) {
#if COMPLETE_OPENMPRUNTIME_IMPLEMENTATION_4_BARRIER
            case ompt_sync_region_barrier_implicit_parallel:
            case ompt_sync_region_barrier_teams:
                if (codeptr_ra == NULL || parallel_codeptr == codeptr_ra) {
                    /* this is the join barrier for the parallel region: if codeptr_ra == NULL: non-master thread;
                     * if parallel_lgp->codeptr_ra == codeptr_ra: master thread */
                    /* this is NOT the actual join_end since by the time sycn region terminates, a thread may already
                     * finish join_end, waiting in the pool and then resummoned for doing the work, so this is actually
                     * when a thread is about to enter into a new parallel region and start an implicit task */
                    if (trace_bit) {
#ifdef PINSIGHT_ENERGY
                        if (global_thread_num == 0) rapl_sysfs_read_packages(package_energy); // Read package energy counters.
#endif
#ifdef PINSIGHT_BACKTRACE
                        retrieve_backtrace();
#endif
                        lttng_ust_tracepoint(ompt_pinsight_lttng_ust, parallel_join_sync_end, 0 ENERGY_LTTNG_UST_TRACEPOINT_CALL_ARGS);
                    }
                }
                break;
            case ompt_sync_region_barrier_explicit: //barrier (explicit)
                ;lexgion_t *lgp = lexgion_end(&record_id);
                lgp->end_codeptr_ra = codeptr_ra;
                if (trace_bit) {
#ifdef PINSIGHT_ENERGY
                    if (global_thread_num == 0) {
                        rapl_sysfs_read_packages(package_energy); // Read package energy counters.
                    }
#endif
#ifdef PINSIGHT_BACKTRACE
                    retrieve_backtrace();
#endif
                    lttng_ust_tracepoint(ompt_pinsight_lttng_ust, barrier_explicit_sync_end, (unsigned short) kind, codeptr_ra, record_id
                                   ENERGY_LTTNG_UST_TRACEPOINT_CALL_ARGS);
                    lexgion_post_trace_update(lgp);
                }
                break;
            case ompt_sync_region_barrier_implementation:
            case ompt_sync_region_barrier_implicit_workshare:
                /* implicit barrier in worksharing, single, sections, and explicit barrier */
                /* each thread will have a lexgion object for the same lexgion */
                if (trace_bit) {
#ifdef PINSIGHT_ENERGY
                    if (global_thread_num == 0) {
                        rapl_sysfs_read_packages(package_energy); // Read package energy counters.
                    }
#endif
#ifdef PINSIGHT_BACKTRACE
                    retrieve_backtrace();
#endif
                    lttng_ust_tracepoint(ompt_pinsight_lttng_ust, barrier_implicit_sync_end, (unsigned short) kind, codeptr_ra
                                   ENERGY_LTTNG_UST_TRACEPOINT_CALL_ARGS);
                }
                break;
#else
            case ompt_sync_region_barrier: //barrier (implicit or explicit)
            case ompt_sync_region_barrier_explicit:
            case ompt_sync_region_barrier_implicit: //for join barrier and implicit barrier such as at the end of a for loop
            case ompt_sync_region_barrier_implementation:
            case ompt_sync_region_barrier_implicit_workshare:
            case ompt_sync_region_barrier_implicit_parallel:
            case ompt_sync_region_barrier_teams:
            	   if (codeptr_ra == NULL || parallel_codeptr == codeptr_ra) {
            	                    /* this is the join barrier for the parallel region: if codeptr_ra == NULL: non-master thread;
            	                     * if parallel_lgp->codeptr_ra == codeptr_ra: master thread */
            	                    /* this is NOT the actual join_end since by the time sycn region terminates, a thread may already
            	                     * finish join_end, waiting in the pool and then resummoned for doing the work, so this is actually
            	                     * when a thread is about to enter into a new parallel region and start an implicit task */
            	                    if (trace_bit) {
            	#ifdef PINSIGHT_ENERGY
            	                        if (global_thread_num == 0) rapl_sysfs_read_packages(package_energy); // Read package energy counters.
            	#endif
#ifdef PINSIGHT_BACKTRACE
                                        retrieve_backtrace();
#endif
            	                        lttng_ust_tracepoint(ompt_pinsight_lttng_ust, parallel_join_sync_end, 0 ENERGY_LTTNG_UST_TRACEPOINT_CALL_ARGS);
            	                    }
            	                } else {
            	                    /* implicit barrier in worksharing, single, sections, and explicit barrier */
            	                    /* each thread will have a lexgion object for the same lexgion */
            	                    ;lexgion_t *lgp = lexgion_end(&record_id);
            	                    lgp->end_codeptr_ra = codeptr_ra;
            	                    if (trace_bit) {
            	    #ifdef PINSIGHT_ENERGY
            	                        if (global_thread_num == 0) {
            	                            rapl_sysfs_read_packages(package_energy); // Read package energy counters.
            	                        }
            	    #endif
#ifdef PINSIGHT_BACKTRACE
                                        retrieve_backtrace();
#endif
            	                        lttng_ust_tracepoint(ompt_pinsight_lttng_ust, barrier_implicit_sync_end, (unsigned short) kind, codeptr_ra
            	                                       ENERGY_LTTNG_UST_TRACEPOINT_CALL_ARGS);
            	                        lexgion_post_trace_update(lgp);
            	                    }
            	                }
            	                break;
#endif
            case ompt_sync_region_taskwait:
            case ompt_sync_region_taskgroup:
            case ompt_sync_region_reduction:
                break;
        }
        break;
    }
  }
}

static void
on_ompt_callback_sync_region_wait(
        ompt_sync_region_t kind,
        ompt_scope_endpoint_t endpoint,
        ompt_data_t *parallel_data,
        ompt_data_t *task_data,
        const void *codeptr_ra) {
	lexgion_record_t * record = top_lexgion(); //this could be either the region that contains the implicit barrier or the explicit barrier
	unsigned int record_id = record->record_id;
    switch(endpoint) {
        case ompt_scope_begin: {
            switch (kind) {
#if COMPLETE_OPENMPRUNTIME_IMPLEMENTATION_4_BARRIER
                case ompt_sync_region_barrier_implicit_parallel:
                case ompt_sync_region_barrier_teams:
                    if (codeptr_ra == NULL || parallel_codeptr == codeptr_ra) { //safety check
                        /* this is the join barrier for the parallel region: if codeptr_ra == NULL: non-master thread;
                         * if parallel_lgp->codeptr_ra == codeptr_ra: master thread */
                        if (trace_bit) {
#ifdef PINSIGHT_ENERGY
                            if (global_thread_num == 0) rapl_sysfs_read_packages(package_energy); // Read package energy counters.
#endif
                            lttng_ust_tracepoint(ompt_pinsight_lttng_ust, parallel_join_sync_wait_begin, 0 ENERGY_LTTNG_UST_TRACEPOINT_CALL_ARGS);
                        }
                    }
                    break;
                case ompt_sync_region_barrier_explicit: //barrier (explicit)
                    if (trace_bit) {
#ifdef PINSIGHT_ENERGY
                        if (global_thread_num == 0) rapl_sysfs_read_packages(package_energy); // Read package energy counters.
#endif
                        lttng_ust_tracepoint(ompt_pinsight_lttng_ust, barrier_explicit_sync_wait_begin, (unsigned short) kind, codeptr_ra, record_id
                                   ENERGY_LTTNG_UST_TRACEPOINT_CALL_ARGS);
                    }
                    break;
                case ompt_sync_region_barrier_implementation:
                case ompt_sync_region_barrier_implicit_workshare:
                    /* implicit barrier in worksharing, single, sections, and explicit barrier */
                    /* each thread will have a lexgion object for the same lexgion in the sync_wait callback */
                    if (trace_bit) {
#ifdef PINSIGHT_ENERGY
                        if (global_thread_num == 0) rapl_sysfs_read_packages(package_energy); // Read package energy counters.
#endif
                        lttng_ust_tracepoint(ompt_pinsight_lttng_ust, barrier_implicit_sync_wait_begin, (unsigned short) kind, codeptr_ra
                                   ENERGY_LTTNG_UST_TRACEPOINT_CALL_ARGS);
                    }
                    break;
#else
            case ompt_sync_region_barrier: //barrier (implicit or explicit)
            case ompt_sync_region_barrier_explicit:
            case ompt_sync_region_barrier_implicit: //for join barrier and implicit barrier such as at the end of a for loop
            case ompt_sync_region_barrier_implementation:
            case ompt_sync_region_barrier_implicit_workshare:
            case ompt_sync_region_barrier_implicit_parallel:
            case ompt_sync_region_barrier_teams:
                    if (codeptr_ra == NULL || parallel_codeptr == codeptr_ra) {
                        /* this is the join barrier for the parallel region: if codeptr_ra == NULL: non-master thread;
                         * if parallel_lgp->codeptr_ra == codeptr_ra: master thread */
                        if (trace_bit) {
#ifdef PINSIGHT_ENERGY
                            if (global_thread_num == 0) rapl_sysfs_read_packages(package_energy); // Read package energy counters.
#endif
                            lttng_ust_tracepoint(ompt_pinsight_lttng_ust, parallel_join_sync_wait_begin, 0 ENERGY_LTTNG_UST_TRACEPOINT_CALL_ARGS);
                        }
                    } else {
                        /* implicit barrier in worksharing, single, sections, and explicit barrier */
                        /* each thread will have a lexgion object for the same lexgion in the sync_wait callback */
                        if (trace_bit) {
#ifdef PINSIGHT_ENERGY
                            if (global_thread_num == 0) rapl_sysfs_read_packages(package_energy); // Read package energy counters.
#endif
                            lttng_ust_tracepoint(ompt_pinsight_lttng_ust, barrier_implicit_sync_wait_begin, (unsigned short) kind, codeptr_ra
                                       ENERGY_LTTNG_UST_TRACEPOINT_CALL_ARGS);
                        }
                    }
                    break;
#endif
                case ompt_sync_region_taskwait:
                case ompt_sync_region_taskgroup:
                case ompt_sync_region_reduction:
                    break;
            }
            break;
        }
        case ompt_scope_end: {
            switch(kind) {
#if COMPLETE_OPENMPRUNTIME_IMPLEMENTATION_4_BARRIER
                case ompt_sync_region_barrier_implicit_parallel:
                case ompt_sync_region_barrier_teams:
                    if (codeptr_ra == NULL || parallel_codeptr == codeptr_ra) {
                        /* this is the join barrier for the parallel region: if codeptr_ra == NULL: non-master thread;
                         * if parallel_lgp->codeptr_ra == codeptr_ra: master thread */
                        /* this is NOT the actual join_end since by the time sync region terminates, a thread may already
                         * finish join_end, waiting in the pool and then resummoned for doing the work, so this is actually
                         * when a thread is about to enter into a new parallel region and start an implicit task */
                        if (trace_bit) {
#ifdef PINSIGHT_ENERGY
                            if (global_thread_num == 0) rapl_sysfs_read_packages(package_energy); // Read package energy counters.
#endif
                            lttng_ust_tracepoint(ompt_pinsight_lttng_ust, parallel_join_sync_wait_end, 0 ENERGY_LTTNG_UST_TRACEPOINT_CALL_ARGS);
                        }
                    }
                    break;
                case ompt_sync_region_barrier_explicit: //barrier (explicit)
                    if (trace_bit) {
#ifdef PINSIGHT_ENERGY
                        if (global_thread_num == 0) {
                            rapl_sysfs_read_packages(package_energy); // Read package energy counters.
                        }
#endif
                        lttng_ust_tracepoint(ompt_pinsight_lttng_ust, barrier_explicit_sync_wait_end, (unsigned short) kind, codeptr_ra, record_id
                                       ENERGY_LTTNG_UST_TRACEPOINT_CALL_ARGS);
                    }
                    break;
                case ompt_sync_region_barrier_implementation:
                case ompt_sync_region_barrier_implicit_workshare:
                    /* implicit barrier in worksharing, single, sections, and explicit barrier */
                    /* each thread will have a lexgion object for the same lexgion */
                    if (trace_bit) {
#ifdef PINSIGHT_ENERGY
                        if (global_thread_num == 0) {
                            rapl_sysfs_read_packages(package_energy); // Read package energy counters.
                        }
#endif
                        lttng_ust_tracepoint(ompt_pinsight_lttng_ust, barrier_implicit_sync_wait_end, (unsigned short) kind, codeptr_ra
                                       ENERGY_LTTNG_UST_TRACEPOINT_CALL_ARGS);
                    }
                    break;
#else
            case ompt_sync_region_barrier: //barrier (implicit or explicit)
            case ompt_sync_region_barrier_explicit:
            case ompt_sync_region_barrier_implicit: //for join barrier and implicit barrier such as at the end of a for loop
            case ompt_sync_region_barrier_implementation:
            case ompt_sync_region_barrier_implicit_workshare:
            case ompt_sync_region_barrier_implicit_parallel:
            case ompt_sync_region_barrier_teams:
            	if (codeptr_ra == NULL || parallel_codeptr == codeptr_ra) {
            	                        /* this is the join barrier for the parallel region: if codeptr_ra == NULL: non-master thread;
            	                         * if parallel_lgp->codeptr_ra == codeptr_ra: master thread */
            	                        /* this is NOT the actual join_end since by the time sycn region terminates, a thread may already
            	                         * finish join_end, waiting in the pool and then resummoned for doing the work, so this is actually
            	                         * when a thread is about to enter into a new parallel region and start an implicit task */
            	                        if (trace_bit) {
            	#ifdef PINSIGHT_ENERGY
            	                            if (global_thread_num == 0) rapl_sysfs_read_packages(package_energy); // Read package energy counters.
            	#endif
            	                            lttng_ust_tracepoint(ompt_pinsight_lttng_ust, parallel_join_sync_wait_end, 0 ENERGY_LTTNG_UST_TRACEPOINT_CALL_ARGS);
            	                        }
            	                    } else {
            	                        /* implicit barrier in worksharing, single, sections, and explicit barrier */
            	                        /* each thread will have a lexgion object for the same lexgion */
            	                        if (trace_bit) {
            	#ifdef PINSIGHT_ENERGY
            	                            if (global_thread_num == 0) {
            	                                rapl_sysfs_read_packages(package_energy); // Read package energy counters.
            	                            }
            	#endif
            	                            lttng_ust_tracepoint(ompt_pinsight_lttng_ust, barrier_implicit_sync_wait_end, (unsigned short) kind, codeptr_ra
            	                                       ENERGY_LTTNG_UST_TRACEPOINT_CALL_ARGS);
            	                        }
            	                    }
            	                    break;
#endif
                case ompt_sync_region_taskwait:
                case ompt_sync_region_taskgroup:
                case ompt_sync_region_reduction:
                    break;
            }
            break;
        }
    }
}

static void
on_ompt_callback_mutex_acquire(
  ompt_mutex_t kind,
  unsigned int hint,
  unsigned int impl,
  ompt_wait_id_t wait_id,
  const void *codeptr_ra)
{
  switch(kind)
  {
    case ompt_mutex_lock:
      if (debug_on) {
        printf("%" PRIu64 ": ompt_event_wait_lock: wait_id=%" PRIu64 ", hint=%" PRIu32 ", impl=%" PRIu32 ", return_address=%p \n", ompt_get_thread_data()->value, wait_id, hint, impl, codeptr_ra);
      }
      break;
    case ompt_mutex_nest_lock:
      if (debug_on) {
        printf("%" PRIu64 ": ompt_event_wait_nest_lock: wait_id=%" PRIu64 ", hint=%" PRIu32 ", impl=%" PRIu32 ", return_address=%p \n", ompt_get_thread_data()->value, wait_id, hint, impl, codeptr_ra);
      }
      break;
    case ompt_mutex_critical:
      if (debug_on) {
        printf("%" PRIu64 ": ompt_event_wait_critical: wait_id=%" PRIu64 ", hint=%" PRIu32 ", impl=%" PRIu32 ", return_address=%p \n", ompt_get_thread_data()->value, wait_id, hint, impl, codeptr_ra);
      }
      break;
    case ompt_mutex_atomic:
      if (debug_on) {
        printf("%" PRIu64 ": ompt_event_wait_atomic: wait_id=%" PRIu64 ", hint=%" PRIu32 ", impl=%" PRIu32 ", return_address=%p \n", ompt_get_thread_data()->value, wait_id, hint, impl, codeptr_ra);
      }
      break;
    case ompt_mutex_ordered:
      if (debug_on) {
        printf("%" PRIu64 ": ompt_event_wait_ordered: wait_id=%" PRIu64 ", hint=%" PRIu32 ", impl=%" PRIu32 ", return_address=%p \n", ompt_get_thread_data()->value, wait_id, hint, impl, codeptr_ra);
      }
      break;
    default:
      break;
  }
}

static void
on_ompt_callback_mutex_acquired(
  ompt_mutex_t kind,
  ompt_wait_id_t wait_id,
  const void *codeptr_ra)
{
  switch(kind)
  {
    case ompt_mutex_lock:
      if (debug_on) {
        printf("%" PRIu64 ": ompt_event_acquired_lock: wait_id=%" PRIu64 ", return_address=%p \n", ompt_get_thread_data()->value, wait_id, codeptr_ra);
      }
      break;
    case ompt_mutex_nest_lock:
      if (debug_on) {
        printf("%" PRIu64 ": ompt_event_acquired_nest_lock_first: wait_id=%" PRIu64 ", return_address=%p \n", ompt_get_thread_data()->value, wait_id, codeptr_ra);
      }
      break;
    case ompt_mutex_critical:
      if (debug_on) {
        printf("%" PRIu64 ": ompt_event_acquired_critical: wait_id=%" PRIu64 ", return_address=%p \n", ompt_get_thread_data()->value, wait_id, codeptr_ra);
      }
      break;
    case ompt_mutex_atomic:
      if (debug_on) {
        printf("%" PRIu64 ": ompt_event_acquired_atomic: wait_id=%" PRIu64 ", return_address=%p \n", ompt_get_thread_data()->value, wait_id, codeptr_ra);
      }
      break;
    case ompt_mutex_ordered:
      if (debug_on) {
        printf("%" PRIu64 ": ompt_event_acquired_ordered: wait_id=%" PRIu64 ", return_address=%p \n", ompt_get_thread_data()->value, wait_id, codeptr_ra);
      }
      break;
    default:
      break;
  }
}

static void
on_ompt_callback_mutex_released(
  ompt_mutex_t kind,
  ompt_wait_id_t wait_id,
  const void *codeptr_ra)
{
  switch(kind)
  {
    case ompt_mutex_lock:
      if (debug_on) {
        printf("%" PRIu64 ": ompt_event_release_lock: wait_id=%" PRIu64 ", return_address=%p \n", ompt_get_thread_data()->value, wait_id, codeptr_ra);
      }
      break;
    case ompt_mutex_nest_lock:
      if (debug_on) {
        printf("%" PRIu64 ": ompt_event_release_nest_lock_last: wait_id=%" PRIu64 ", return_address=%p \n", ompt_get_thread_data()->value, wait_id, codeptr_ra);
      }
      break;
    case ompt_mutex_critical:
      if (debug_on) {
        printf("%" PRIu64 ": ompt_event_release_critical: wait_id=%" PRIu64 ", return_address=%p \n", ompt_get_thread_data()->value, wait_id, codeptr_ra);
      }
      break;
    case ompt_mutex_atomic:
      if (debug_on) {
        printf("%" PRIu64 ": ompt_event_release_atomic: wait_id=%" PRIu64 ", return_address=%p \n", ompt_get_thread_data()->value, wait_id, codeptr_ra);
      }
      break;
    case ompt_mutex_ordered:
      if (debug_on) {
        printf("%" PRIu64 ": ompt_event_release_ordered: wait_id=%" PRIu64 ", return_address=%p \n", ompt_get_thread_data()->value, wait_id, codeptr_ra);
      }
      break;
    default:
      break;
  }
}

static void
on_ompt_callback_nest_lock(
    ompt_scope_endpoint_t endpoint,
    ompt_wait_id_t wait_id,
    const void *codeptr_ra)
{
  switch(endpoint)
  {
    case ompt_scope_begin:
      if (debug_on) {
        printf("%" PRIu64 ": ompt_event_acquired_nest_lock_next: wait_id=%" PRIu64 ", return_address=%p \n", ompt_get_thread_data()->value, wait_id, codeptr_ra);
      }
      break;
    case ompt_scope_end:
      if (debug_on) {
        printf("%" PRIu64 ": ompt_event_release_nest_lock_prev: wait_id=%" PRIu64 ", return_address=%p \n", ompt_get_thread_data()->value, wait_id, codeptr_ra);
      }
      break;
  }
}

static void
on_ompt_callback_flush(
    ompt_data_t *thread_data,
    const void *codeptr_ra)
{
  if (debug_on) {
    printf("%" PRIu64 ": ompt_event_flush: codeptr_ra=%p\n", thread_data->value, codeptr_ra);
  }
}

static void
on_ompt_callback_cancel(
    ompt_data_t *task_data,
    int flags,
    const void *codeptr_ra)
{
  const char* first_flag_value;
  const char* second_flag_value;
  if(flags & ompt_cancel_parallel)
    first_flag_value = ompt_cancel_flag_t_values[0];
  else if(flags & ompt_cancel_sections)
    first_flag_value = ompt_cancel_flag_t_values[1];
  else if(flags & ompt_cancel_loop)
    first_flag_value = ompt_cancel_flag_t_values[2];
  else if(flags & ompt_cancel_taskgroup)
    first_flag_value = ompt_cancel_flag_t_values[3];

  if(flags & ompt_cancel_activated)
    second_flag_value = ompt_cancel_flag_t_values[4];
  else if(flags & ompt_cancel_detected)
    second_flag_value = ompt_cancel_flag_t_values[5];
  else if(flags & ompt_cancel_discarded_task)
    second_flag_value = ompt_cancel_flag_t_values[6];
    
  if (debug_on) {
    printf("%" PRIu64 ": ompt_event_cancel: task_data=%" PRIu64 ", flags=%s|%s=%" PRIu32 ", codeptr_ra=%p\n", ompt_get_thread_data()->value, task_data->value, first_flag_value, second_flag_value, flags,  codeptr_ra);
  }
}

// Note: Obsoleted in TR7, as it was weird/impossible to implement correctly.
//static void
//on_ompt_callback_idle(
//  ompt_scope_endpoint_t endpoint)
//{
//  switch(endpoint)
//  {
//    case ompt_scope_begin:
//      lttng_ust_tracepoint(ompt_pinsight_lttng_ust, global_thread_numle_begin, global_thread_num, ompt_get_thread_data());
//      //printf("%" PRIu64 ": ompt_event_idle_begin:\n", ompt_get_thread_data()->value);
//      //printf("%" PRIu64 ": ompt_event_idle_begin: global_thread_num=%" PRIu64 "\n", ompt_get_thread_data()->value, thread_data.value);
//      break;
//    case ompt_scope_end:
//      lttng_ust_tracepoint(ompt_pinsight_lttng_ust, global_thread_numle_end, global_thread_num, ompt_get_thread_data());
//      //printf("%" PRIu64 ": ompt_event_idle_end:\n", ompt_get_thread_data()->value);
//      //printf("%" PRIu64 ": ompt_event_idle_end: global_thread_num=%" PRIu64 "\n", ompt_get_thread_data()->value, thread_data.value);
//      break;
//  }
//}

static void
on_ompt_callback_lock_init(
  ompt_mutex_t kind,
  unsigned int hint,
  unsigned int impl,
  ompt_wait_id_t wait_id,
  const void *codeptr_ra)
{
  switch(kind)
  {
    case ompt_mutex_lock:
      if (debug_on) {
        printf("%" PRIu64 ": ompt_event_init_lock: wait_id=%" PRIu64 ", hint=%" PRIu32 ", impl=%" PRIu32 ", return_address=%p \n", ompt_get_thread_data()->value, wait_id, hint, impl, codeptr_ra);
      }
      break;
    case ompt_mutex_nest_lock:
      if (debug_on) {
        printf("%" PRIu64 ": ompt_event_init_nest_lock: wait_id=%" PRIu64 ", hint=%" PRIu32 ", impl=%" PRIu32 ", return_address=%p \n", ompt_get_thread_data()->value, wait_id, hint, impl, codeptr_ra);
      }
      break;
    default:
      break;
  }
}

static void
on_ompt_callback_lock_destroy(
  ompt_mutex_t kind,
  ompt_wait_id_t wait_id,
  const void *codeptr_ra)
{
  switch(kind)
  {
    case ompt_mutex_lock:
      if (debug_on) {
        printf("%" PRIu64 ": ompt_event_destroy_lock: wait_id=%" PRIu64 ", return_address=%p \n", ompt_get_thread_data()->value, wait_id, codeptr_ra);
      }
      break;
    case ompt_mutex_nest_lock:
      if (debug_on) {
        printf("%" PRIu64 ": ompt_event_destroy_nest_lock: wait_id=%" PRIu64 ", return_address=%p \n", ompt_get_thread_data()->value, wait_id, codeptr_ra);
      }
      break;
    default:
      break;
  }
}

static void
on_ompt_callback_task_create(
        ompt_data_t *encountering_task_data,
        const ompt_frame_t *encountering_task_frame,
        ompt_data_t* new_task_data,
        int type,
        int has_dependences,
        const void *codeptr_ra) {
  if (new_task_data->ptr)
    printf("0: new_task_data initially not null\n");

  //there is no parallel_begin callback for implicit parallel region
  //thus it is initialized in initial task
  if (type & ompt_task_initial) {
    /* the initial task */
    //lexgion_t * lgp = lexgion_begin(ompt_callback_task_create, (void*)0xFFFFFFFF, &new_task_data->value);
  }
}

static void
on_ompt_callback_task_schedule(
    ompt_data_t *first_task_data,
    ompt_task_status_t prior_task_status,
    ompt_data_t *second_task_data)
{
  if (debug_on) {
    printf("%" PRIu64 ": ompt_event_task_schedule: first_task_id=%" PRIu64 ", second_task_id=%" PRIu64 ", prior_task_status=%s=%d\n", ompt_get_thread_data()->value, first_task_data->value, second_task_data->value, ompt_task_status_t_values[prior_task_status], prior_task_status);
  }
  if(prior_task_status == ompt_task_complete)
  {
    if (debug_on) {
      printf("%" PRIu64 ": ompt_event_task_end: task_id=%" PRIu64 "\n", ompt_get_thread_data()->value, first_task_data->value);
    }
  }
}

static void
on_ompt_callback_dependences(
  ompt_data_t *task_data,
  const ompt_dependence_t *deps,
  int ndeps)
{

  if (debug_on) {
    printf("%" PRIu64 ": ompt_event_task_dependences: task_id=%" PRIu64 ", deps=%p, ndeps=%d\n", ompt_get_thread_data()->value, task_data->value, (void *)deps, ndeps);
  }
}

static void
on_ompt_callback_task_dependence(
  ompt_data_t *first_task_data,
  ompt_data_t *second_task_data)
{
  if (debug_on) {
    printf("%" PRIu64 ": ompt_event_task_dependence_pair: first_task_id=%" PRIu64 ", second_task_id=%" PRIu64 "\n", ompt_get_thread_data()->value, first_task_data->value, second_task_data->value);
  }
}

static int
on_ompt_callback_control_tool(
  uint64_t command,
  uint64_t modifier,
  void *arg,
  const void *codeptr_ra)
{
  if (debug_on) {
    printf("%" PRIu64 ": ompt_event_control_tool: command=%" PRIu64 ", modifier=%" PRIu64 ", arg=%p, codeptr_ra=%p\n", ompt_get_thread_data()->value, command, modifier, arg, codeptr_ra);
  }
  return 0; //success
}

#define register_callback_t(name, type)                       \
do {                                                          \
  type f_##name = &on_##name;                                 \
  if (ompt_set_callback(name, (ompt_callback_t)f_##name) ==   \
      ompt_set_never) {                                       \
    if (debug_on) {                                           \
      printf("0: Could not register callback '" #name "'\n"); \
    }                                                         \
  }                                                           \
} while(0)

#define register_callback(name) register_callback_t(name, name##_t)

int ompt_initialize(
  ompt_function_lookup_t lookup,
  int initial_device_num,
  ompt_data_t *tool_data)
{
  ompt_set_callback = (ompt_set_callback_t) lookup("ompt_set_callback");
  ompt_get_task_info = (ompt_get_task_info_t) lookup("ompt_get_task_info");
  ompt_get_thread_data = (ompt_get_thread_data_t) lookup("ompt_get_thread_data");
  ompt_get_parallel_info = (ompt_get_parallel_info_t) lookup("ompt_get_parallel_info");
  ompt_get_unique_id = (ompt_get_unique_id_t) lookup("ompt_get_unique_id");

  ompt_get_num_places = (ompt_get_num_places_t) lookup("ompt_get_num_places");
  ompt_get_place_proc_ids = (ompt_get_place_proc_ids_t) lookup("ompt_get_place_proc_ids");
  ompt_get_place_num = (ompt_get_place_num_t) lookup("ompt_get_place_num");
  ompt_get_partition_place_nums = (ompt_get_partition_place_nums_t) lookup("ompt_get_partition_place_nums");
  ompt_get_proc_id = (ompt_get_proc_id_t) lookup("ompt_get_proc_id");

//  register_callback(ompt_callback_mutex_acquire);
//  register_callback_t(ompt_callback_mutex_acquired, ompt_callback_mutex_t);
//  register_callback_t(ompt_callback_mutex_released, ompt_callback_mutex_t);
//  register_callback(ompt_callback_nest_lock);
  register_callback(ompt_callback_sync_region);
  register_callback_t(ompt_callback_sync_region_wait, ompt_callback_sync_region_t);
//  register_callback(ompt_callback_control_tool);
//  register_callback(ompt_callback_flush);
//  register_callback(ompt_callback_cancel);
  //register_callback(ompt_callback_idle);  // Note: Obsoleted in TR7, as it was weird/impossible to implement correctly.
  register_callback(ompt_callback_implicit_task);
//  register_callback_t(ompt_callback_lock_init, ompt_callback_mutex_acquire_t);
//  register_callback_t(ompt_callback_lock_destroy, ompt_callback_mutex_t);
  register_callback(ompt_callback_work);
  register_callback(ompt_callback_masked);
  register_callback(ompt_callback_parallel_begin);
  register_callback(ompt_callback_parallel_end);
//  register_callback(ompt_callback_task_create);
//  register_callback(ompt_callback_task_schedule);
//  register_callback(ompt_callback_task_dependences);
//  register_callback(ompt_callback_task_dependence);
  register_callback(ompt_callback_thread_begin);
  register_callback(ompt_callback_thread_end);

  // Query environment variables to enable/dsiable debug printouts.
  debug_on = env_get_long(PINSIGHT_DEBUG_ENABLE, PINSIGHT_DEBUG_ENABLE_DEFAULT);
  if (debug_on) {
    printf("0: NULL_POINTER=%p\n", NULL);
  }

#ifdef PINSIGHT_ENERGY
  // Initialize RAPL subsystem.
  rapl_sysfs_discover_valid();
#endif

  return 1; //success
}

void ompt_finalize(ompt_data_t *tool_data)
{
  //printf("%d: ompt_event_runtime_shutdown\n", omp_get_thread_num());
}

#ifdef __cplusplus
extern "C" {
#endif
ompt_start_tool_result_t* ompt_start_tool(
        unsigned int omp_version,
        const char *runtime_version)
{
    static ompt_start_tool_result_t ompt_start_tool_result = {&ompt_initialize,&ompt_finalize, 0};
    return &ompt_start_tool_result;
}
#ifdef __cplusplus
}
#endif
