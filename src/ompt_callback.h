#include <stdio.h>
#include <inttypes.h>
#include <omp.h>
#include <ompt.h>
#include <execinfo.h>
#ifdef OMPT_USE_LIBUNWIND
#define UNW_LOCAL_ONLY
#include <libunwind.h>
#endif
#include "env_config.h"
#include "pinsight.h"

// --------------------------------------------------------
// Environment config variable names and default values.

#define PINSIGHT_DEBUG_ENABLE "PINSIGHT_DEBUG_ENABLE"
#define PINSIGHT_DEBUG_ENABLE_DEFAULT 0

// Configuration settings.
static int debug_on;

#ifdef ENABLE_ENERGY
#include "rapl.h"
// --------------------------------------------------------
// RAPL package values.
static long long package_energy[MAX_PACKAGES];
#endif

#define TRACEPOINT_CREATE_PROBES
#define TRACEPOINT_DEFINE
#include "lttng_tracepoint.h"
extern int __kmpc_global_thread_num(void *);
extern int __kmpc_global_num_threads(void *);

#define get_global_thread_num() __kmpc_global_thread_num(NULL)
#define get_global_num_threads() __kmpc_global_num_threads(NULL)

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
#define OUTMOST_CODEPTR 0x00000FFFFFF
static void
on_ompt_callback_thread_begin(
        ompt_thread_t thread_type,
        ompt_data_t *thread_data)
{
  init_thread_data(get_global_thread_num(), thread_type);
  thread_data->value = global_thread_num;

  //This is the codeptr for the first lexgion of each thread
  const void * codeptr_ra = (void*)OUTMOST_CODEPTR;
  ompt_lexgion_t * lgp = ompt_lexgion_begin(ompt_callback_thread_begin, codeptr_ra);
  lgp->counter++; //counter only increment
  push_lexgion(lgp, lgp->counter);

  parallel_codeptr = codeptr_ra;
  parallel_counter = 1;
  omp_thread_num = -1;
  task_codeptr = codeptr_ra; //The same as this lexgion
  task_counter = 1;

  //initial task initialization is done at the task_create callback
#ifdef ENABLE_ENERGY
  if (global_thread_num == 0) {
    rapl_sysfs_read_packages(package_energy); // Read package energy counters.
  }
#endif
  tracepoint(lttng_pinsight, thread_begin, (short)thread_type ENERGY_TRACEPOINT_CALL_ARGS);
}

static void
on_ompt_callback_thread_end(
        ompt_data_t *thread_data)
{
#ifdef ENABLE_ENERGY
  if (global_thread_num == 0) {
    rapl_sysfs_read_packages(package_energy); // Read package energy counters.
  }
#endif
  unsigned int counter;
  ompt_lexgion_t * lgp = ompt_lexgion_end(&counter);
  assert(lgp->codeptr_ra == (void*)OUTMOST_CODEPTR);
  task_codeptr = lgp->codeptr_ra;
  task_counter = counter;
  assert(counter == lgp->counter);
  lgp->end_codeptr_ra = (void*)OUTMOST_CODEPTR;
  tracepoint(lttng_pinsight, thread_end, (short)pinsight_thread_data.thread_type ENERGY_TRACEPOINT_CALL_ARGS);
  ompt_lexgion_post_trace_update(lgp);

  //print out lexgion summary */
  if (global_thread_num == 0) {
    printf("============================================================\n");
    printf("Lexgion report from thread 0: total %d lexgions\n", pinsight_thread_data.num_lexgions);
    printf("#\tcodeptr_ra\tcount\ttrace count\ttype\tend_codeptr_ra\n");
    int i;
    int count;
    for (i=0; i<pinsight_thread_data.num_lexgions; i++) {
      ompt_lexgion_t* lgp = &pinsight_thread_data.lexgions[i];
      printf("%d\t%p\t%d\t\t%d\t%d\t%p\n", i+1, lgp->codeptr_ra, lgp->counter, lgp->trace_counter, lgp->type, lgp->end_codeptr_ra);
    }

    printf("-------------------------------------------------------------\n");
    printf("parallel lexgions (type %d) from thread 0\n", ompt_callback_parallel_begin);
    printf("#\tcodeptr_ra\tcount\ttrace count\ttype\tend_codeptr_ra\n");
    count = 1;
    for (i=0; i<pinsight_thread_data.num_lexgions; i++) {
      ompt_lexgion_t* lgp = &pinsight_thread_data.lexgions[i];
      if (lgp->type == ompt_callback_parallel_begin)
        printf("%d\t%p\t%d\t\t%d\t%d\t%p\n", count++, lgp->codeptr_ra, lgp->counter, lgp->trace_counter, lgp->type, lgp->end_codeptr_ra);
    }

    printf("-------------------------------------------------------------\n");
    printf("sync lexgions (type %d) from thread 0\n", ompt_callback_sync_region);
    printf("#\tcodeptr_ra\tcount\ttrace count\ttype\tend_codeptr_ra\n");
    count = 1;
    for (i=0; i<pinsight_thread_data.num_lexgions; i++) {
      ompt_lexgion_t* lgp = &pinsight_thread_data.lexgions[i];
      if (lgp->type == ompt_callback_sync_region)
        printf("%d\t%p\t%d\t\t%d\t%d\t%p\n", count++, lgp->codeptr_ra, lgp->counter, lgp->trace_counter, lgp->type, lgp->end_codeptr_ra);
    }

    printf("-------------------------------------------------------------\n");
    printf("work lexgions (type %d) from thread 0\n", ompt_callback_work);
    printf("#\tcodeptr_ra\tcount\ttrace count\ttype\tend_codeptr_ra\n");
    count = 1;
    for (i=0; i<pinsight_thread_data.num_lexgions; i++) {
      ompt_lexgion_t* lgp = &pinsight_thread_data.lexgions[i];
      if (lgp->type == ompt_callback_work)
        printf("%d\t%p\t%d\t\t%d\t%d\t%p\n", count++, lgp->codeptr_ra, lgp->counter, lgp->trace_counter, lgp->type, lgp->end_codeptr_ra);
    }

    printf("-------------------------------------------------------------\n");
    printf("master lexgions (type %d) from thread 0\n", ompt_callback_master);
    printf("#\tcodeptr_ra\tcount\ttrace count\ttype\tend_codeptr_ra\n");
    count = 1;
    for (i=0; i<pinsight_thread_data.num_lexgions; i++) {
      ompt_lexgion_t* lgp = &pinsight_thread_data.lexgions[i];
      if (lgp->type == ompt_callback_master)
        printf("%d\t%p\t%d\t\t%d\t%d\t%p\n", count++, lgp->codeptr_ra, lgp->counter, lgp->trace_counter, lgp->type, lgp->end_codeptr_ra);
    }
    printf("============================================================\n");

  }
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
//  parallel_data->value = ompt_get_unique_id();
  ompt_lexgion_t * lgp = ompt_lexgion_begin(ompt_callback_parallel_begin, codeptr_ra);
  lgp->counter++;
  lgp->num_exes_after_last_trace ++;
  push_lexgion(lgp, lgp->counter);
  //This parallel_data->value will be passed to the implicit tasks, which use this number to get codeptr and counter
  parallel_data->value = LEXGION_RECORD_UUID(codeptr_ra, lgp->counter); //we use [codeptr_ra][counter]-formatted uuid

//  parallel_codeptr = codeptr_ra; //redundant since implicit_task will do this
//  parallel_counter = lgp->counter; //redundant since implicit task will do this
//  omp_thread_num = 0;  //redundant since implicit task will do this
  ompt_set_trace(lgp);
  if (trace_bit) {
#ifdef ENABLE_ENERGY
    if (global_thread_num == 0) {
      rapl_sysfs_read_packages(package_energy); // Read package energy counters.
    }
#endif
    tracepoint(lttng_pinsight, parallel_begin, requested_team_size, flag,
               parent_task_frame->enter_frame.ptr ENERGY_TRACEPOINT_CALL_ARGS);
  }
}

static void
on_ompt_callback_parallel_end(
        ompt_data_t *parallel_data,
        ompt_data_t *parent_task,
        int flag,
        const void *codeptr_ra)
{
  ompt_lexgion_t * lgp = ompt_lexgion_end(NULL);
  lgp->end_codeptr_ra = codeptr_ra;
  assert (lgp->codeptr_ra == codeptr_ra); /* for parallel region and parallel_end event */
  if (trace_bit) {
#ifdef ENABLE_ENERGY
    if (global_thread_num == 0) {
      rapl_sysfs_read_packages(package_energy); // Read package energy counters.
    }
#endif

    tracepoint(lttng_pinsight, parallel_end, flag ENERGY_TRACEPOINT_CALL_ARGS);
    ompt_lexgion_post_trace_update(lgp);
  }
  /* find the topmost parallel lexgion in the stack (in the nested parallel situation) */
  unsigned int counter;
  implicit_task = top_lexgion_type(ompt_callback_implicit_task, NULL); /* the nested situation */
  ompt_lexgion_t * enclosing_parallel = top_lexgion_type(ompt_callback_parallel_begin, &counter);
  if (enclosing_parallel == NULL) {
    parallel_codeptr = (void*) OUTMOST_CODEPTR;
    parallel_counter = 1;
  } else {
    parallel_codeptr = enclosing_parallel->codeptr_ra;
    parallel_counter = counter;
    /* this is not necessarily the lgp-codeptr since the same region may be invoked more than once, e.g. in recursive parallel region
     * lgp->counter is the counter for the most recent parallel of this region, but not necessarily this parallel_end one */
  }
  /* TODO: we need to store and restore the omp_thread_num, task_codeptr, task_counter in the nested parallel situation */

}

static void
on_ompt_callback_implicit_task(
        ompt_scope_endpoint_t endpoint,
        ompt_data_t *parallel_data,
        ompt_data_t *task_data,
        unsigned int team_size,
        unsigned int thread_num)
{
  switch(endpoint)
  {
    case ompt_scope_begin: {
      parallel_codeptr = LEXGION_RECORD_CODEPTR_RA(parallel_data->value);
      parallel_counter = LEXGION_RECORD_COUNTER(parallel_data->value);
      task_codeptr = parallel_codeptr;
      task_counter = parallel_counter;
      omp_thread_num = thread_num;
      /* in this call back, parallel_data is NULL for ompt_scope_end endpoint, thus to know the parallel_data at the end,
       * we need to pass the needed fields of parallel_data in the scope_begin to the task_data */
      task_data->value = parallel_data->value; // Here we just save the parallel_data to the task
      implicit_task = ompt_lexgion_begin(ompt_callback_implicit_task, parallel_codeptr);
      /* here no need to increment the counter since we this is the same instance as when
       * the parallel_begin starts the parallel region */
      implicit_task->counter++; /* this should be the same as parallel_counter; */
      implicit_task->num_exes_after_last_trace++;
      push_lexgion(implicit_task, implicit_task->counter);
      ompt_set_trace(implicit_task);
      if (trace_bit) {
#ifdef ENABLE_ENERGY
        if (global_thread_num == 0) {
          rapl_sysfs_read_packages(package_energy); // Read package energy counters.
        }
#endif
        tracepoint(lttng_pinsight, implicit_task_begin, team_size ENERGY_TRACEPOINT_CALL_ARGS);
      }
      break;
    }
    case ompt_scope_end: {
      ompt_lexgion_t * lgp = ompt_lexgion_end(NULL); /* this is the same as implicit_task */
      if (trace_bit) {
#ifdef ENABLE_ENERGY
        if (global_thread_num == 0) {
          rapl_sysfs_read_packages(package_energy); // Read package energy counters.
        }
#endif
        tracepoint(lttng_pinsight, implicit_task_end, team_size ENERGY_TRACEPOINT_CALL_ARGS);
        ompt_lexgion_post_trace_update(lgp);
      }
      /* find the topmost task lexgion instance in the stack (in the nested situation) */
      unsigned int counter;
      implicit_task = top_lexgion_type(ompt_callback_implicit_task, NULL); /* the nested situation */
      ompt_lexgion_t * enclosing_task = top_lexgion_type(ompt_callback_task_create, &counter);
      if (enclosing_task == NULL) {
        task_codeptr = (void*) OUTMOST_CODEPTR;
        task_counter = 1;
      } else { /* NEED a test code that can triger this path, nested parallel region (?) */
        task_codeptr = enclosing_task->codeptr_ra;
        task_counter = counter;
        /* this is not necessnarily the lgp-codeptr since the same region may be invoked more than once, e.g. in recursive parallel region
         * lgp->counter is the counter for the most recent parallel of this region, but not necessnarily this parallel_end one */
      }
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
        ompt_lexgion_t * lgp = ompt_lexgion_begin(ompt_callback_work, codeptr_ra);
        lgp->counter++;
        push_lexgion(lgp, lgp->counter);

        if (trace_bit) {
#ifdef ENABLE_ENERGY
          if (global_thread_num == 0) {
            rapl_sysfs_read_packages(package_energy); // Read package energy counters.
          }
#endif
          tracepoint(lttng_pinsight, work_begin, (short) wstype, codeptr_ra, (void *) 0x000000, lgp->counter,
                     count ENERGY_TRACEPOINT_CALL_ARGS);
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
      unsigned int counter;
      ompt_lexgion_t *lgp = ompt_lexgion_end(&counter);
      lgp->end_codeptr_ra = codeptr_ra;
      if (lgp->codeptr_ra != parallel_codeptr && lgp->codeptr_ra != task_codeptr) {/* safety check */
        if (trace_bit) {
#ifdef ENABLE_ENERGY
          if (global_thread_num == 0) {
            rapl_sysfs_read_packages(package_energy); // Read package energy counters.
          }
#endif
          tracepoint(lttng_pinsight, work_end, (short) wstype, lgp->codeptr_ra, codeptr_ra, counter,
                     count ENERGY_TRACEPOINT_CALL_ARGS);
          ompt_lexgion_post_trace_update(lgp);
        }
      } else {
        fprintf(stderr, "The work_scope_end lexgion codeptr is the same as enclosing parallel or task codeptr, "
                "something wrong. %p", codeptr_ra);
      }
      break;
  }
}

static void
on_ompt_callback_master(
        ompt_scope_endpoint_t endpoint,
        ompt_data_t *parallel_data,
        ompt_data_t *task_data,
        const void *codeptr_ra)
{
  switch(endpoint)
  {
    case ompt_scope_begin:
      ;
      ompt_lexgion_t * lgp = ompt_lexgion_begin(ompt_callback_master, codeptr_ra);
      lgp->counter++;
      push_lexgion(lgp, lgp->counter);
      if (trace_bit) {
#ifdef ENABLE_ENERGY
        if (global_thread_num == 0) {
          rapl_sysfs_read_packages(package_energy); // Read package energy counters.
        }
#endif
        tracepoint(lttng_pinsight, master_begin, codeptr_ra, (void *) 0x000000, lgp->counter
                   ENERGY_TRACEPOINT_CALL_ARGS);
      }
      break;
    case ompt_scope_end:
      ;
      unsigned int counter;
      lgp = ompt_lexgion_end(&counter);
      lgp->end_codeptr_ra = codeptr_ra;

      if (trace_bit) {
#ifdef ENABLE_ENERGY
        if (global_thread_num == 0) {
          rapl_sysfs_read_packages(package_energy); // Read package energy counters.
        }
#endif
        tracepoint(lttng_pinsight, master_end, lgp->codeptr_ra, codeptr_ra, counter ENERGY_TRACEPOINT_CALL_ARGS);
        ompt_lexgion_post_trace_update(lgp);
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
        const void *codeptr_ra)
{
  switch(endpoint)
  {
    case ompt_scope_begin:
      if (codeptr_ra == NULL || parallel_codeptr == codeptr_ra) {
        /* this is the join barrier for the parallel region: if codeptr_ra == NULL: non-master thread;
         * if parallel_lgp->codeptr_ra == codeptr_ra: master thread */
        if (trace_bit) {
#ifdef ENABLE_ENERGY
          if (global_thread_num == 0) {
            rapl_sysfs_read_packages(package_energy); // Read package energy counters.
          }
#endif
          tracepoint(lttng_pinsight, parallel_join_begin, 0 ENERGY_TRACEPOINT_CALL_ARGS);
        }
      } else {
        /* implicit barrier in worksharing, single, sections, and explicit barrier */
        /* each thread will have a lexgion object for the same lexgion */
        ompt_lexgion_t * lgp = ompt_lexgion_begin(ompt_callback_sync_region, codeptr_ra);
        lgp->counter++;
        push_lexgion(lgp, lgp->counter);
        if (trace_bit) {
#ifdef ENABLE_ENERGY
            if (global_thread_num == 0) {
              rapl_sysfs_read_packages(package_energy); // Read package energy counters.
            }
#endif
            tracepoint(lttng_pinsight, sync_begin, (unsigned short) kind, codeptr_ra, lgp->counter
                       ENERGY_TRACEPOINT_CALL_ARGS);
        }
      }
      switch(kind)
      {
        case ompt_sync_region_barrier:
          break;
        case ompt_sync_region_taskwait:
          break;
        case ompt_sync_region_taskgroup:
          break;
      }
      break;
    case ompt_scope_end:
      if (codeptr_ra == NULL || parallel_codeptr == codeptr_ra) {
        /* this is the join barrier for the parallel region: if codeptr_ra == NULL: non-master thread;
         * if parallel_lgp->codeptr_ra == codeptr_ra: master thread */
        /* this is NOT the actual join_end since by the time sycn region terminates, a thread may already
         * finish join_end, waiting in the pool and then resummoned for doing the work, so this is actually
         * when a thread is about to enter into a parallel region and start an implicit task */
      } else {
        /* implicit barrier in worksharing, single, sections, and explicit barrier */
        /* each thread will have a lexgion object for the same lexgion */
        unsigned int counter;
        ompt_lexgion_t * lgp = ompt_lexgion_end(&counter);
        lgp->end_codeptr_ra = codeptr_ra;
        if (trace_bit) {
#ifdef ENABLE_ENERGY
            if (global_thread_num == 0) {
              rapl_sysfs_read_packages(package_energy); // Read package energy counters.
            }
#endif
            tracepoint(lttng_pinsight, sync_end, (unsigned short) kind, codeptr_ra, counter
                       ENERGY_TRACEPOINT_CALL_ARGS);
            ompt_lexgion_post_trace_update(lgp);
        }
      }
      switch(kind)
      {
        case ompt_sync_region_barrier:
          break;
        case ompt_sync_region_taskwait:
          break;
        case ompt_sync_region_taskgroup:
          break;
      }
      break;
  }
}

static void
on_ompt_callback_sync_region_wait(
        ompt_sync_region_t kind,
        ompt_scope_endpoint_t endpoint,
        ompt_data_t *parallel_data,
        ompt_data_t *task_data,
        const void *codeptr_ra)
{
  unsigned int counter;
  ompt_lexgion_t * lgp = top_lexgion(&counter);
  switch(endpoint)
  {
    case ompt_scope_begin:
      if (codeptr_ra == NULL || parallel_codeptr == codeptr_ra) {
        /* this is the join barrier for the parallel region: if codeptr_ra == NULL: non-master thread;
        * if parallel_lgp->codeptr_ra == codeptr_ra: master thread */
      } else {
        //lgp = ompt_lexgion_begin(ompt_callback_sync_region_wait, codeptr_ra);
        //lgp->counter++;  //We do not increment the counter here since we will use the same counter as the sync_begin
        //push_lexgion(lgp, lgp->counter);
        if (trace_bit) {
#ifdef ENABLE_ENERGY
            if (global_thread_num == 0) {
              rapl_sysfs_read_packages(package_energy); // Read package energy counters.
            }
#endif
            tracepoint(lttng_pinsight, sync_wait_begin, (unsigned short) kind, codeptr_ra, lgp->counter
                       ENERGY_TRACEPOINT_CALL_ARGS);
        }
      }
      switch(kind)
      {
        case ompt_sync_region_barrier:
          break;
        case ompt_sync_region_taskwait:
          break;
        case ompt_sync_region_taskgroup:
          break;
      }
      break;
    case ompt_scope_end:
      if (codeptr_ra == NULL || parallel_codeptr == codeptr_ra) {
        /* this is the join barrier for the parallel region: if codeptr_ra == NULL: non-master thread;
         * if parallel_lgp->codeptr_ra == codeptr_ra: master thread */
        /* this is NOT join_end */
      } else {
        /* implicit barrier in worksharing, single, sections, and explicit barrier */
        /* each thread will have a lexgion object for the same lexgion */
        //lgp = ompt_lexgion_end(&counter);
        //lgp->end_codeptr_ra = codeptr_ra;
        if (trace_bit) {
#ifdef ENABLE_ENERGY
            if (global_thread_num == 0) {
              rapl_sysfs_read_packages(package_energy); // Read package energy counters.
            }
#endif
            tracepoint(lttng_pinsight, sync_wait_end, (unsigned short) kind, codeptr_ra, counter
                       ENERGY_TRACEPOINT_CALL_ARGS);
            //ompt_lexgion_post_trace_update(lgp);
        }
      }
      switch(kind)
      {
        case ompt_sync_region_barrier:
          break;
        case ompt_sync_region_taskwait:
          break;
        case ompt_sync_region_taskgroup:
          break;
      }
      break;
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
//      tracepoint(lttng_pinsight, global_thread_numle_begin, global_thread_num, ompt_get_thread_data());
//      //printf("%" PRIu64 ": ompt_event_idle_begin:\n", ompt_get_thread_data()->value);
//      //printf("%" PRIu64 ": ompt_event_idle_begin: global_thread_num=%" PRIu64 "\n", ompt_get_thread_data()->value, thread_data.value);
//      break;
//    case ompt_scope_end:
//      tracepoint(lttng_pinsight, global_thread_numle_end, global_thread_num, ompt_get_thread_data());
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
    //ompt_lexgion_t * lgp = ompt_lexgion_begin(ompt_callback_task_create, (void*)0xFFFFFFFF, &new_task_data->value);
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
on_ompt_callback_task_dependences(
  ompt_data_t *task_data,
  const ompt_task_dependence_t *deps,
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
//  register_callback_t(ompt_callback_sync_region_wait, ompt_callback_sync_region_t);
//  register_callback(ompt_callback_control_tool);
//  register_callback(ompt_callback_flush);
//  register_callback(ompt_callback_cancel);
  //register_callback(ompt_callback_idle);  // Note: Obsoleted in TR7, as it was weird/impossible to implement correctly.
  register_callback(ompt_callback_implicit_task);
  register_callback_t(ompt_callback_lock_init, ompt_callback_mutex_acquire_t);
//  register_callback_t(ompt_callback_lock_destroy, ompt_callback_mutex_t);
  register_callback(ompt_callback_work);
  register_callback(ompt_callback_master);
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

  const char* pinsight_trace_config = getenv("PINSIGHT_TRACE_CONFIG");
  if (pinsight_trace_config != NULL) {
      printf("PINSIGHT_TRACE_CONFIG: %s\n", pinsight_trace_config);
      sscanf(pinsight_trace_config, "%d:%d:%d", &NUM_INITIAL_TRACES, &MAX_NUM_TRACES, &TRACE_SAMPLING_RATE);
      printf("NUM_INITIAL_TRACES: %u, MAX_NUM_TRACES: %u, TRACE_SAMPLING_RATE: %u\n",
             NUM_INITIAL_TRACES, MAX_NUM_TRACES, TRACE_SAMPLING_RATE);
  }

#ifdef ENABLE_ENERGY
  // Initialize RAPL subsystem.
  rapl_sysfs_discover_valid();
#endif

  return 1; //success
}

void ompt_finalize()
{
  //printf("%d: ompt_event_runtime_shutdown\n", omp_get_thread_num());
}

ompt_start_tool_result_t* ompt_start_tool(
  unsigned int omp_version,
  const char *runtime_version)
{
  static ompt_start_tool_result_t ompt_fns = {&ompt_initialize, &ompt_finalize, 0};
  return &ompt_fns;
}
