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
  omp_frame_t* frame ;
  ompt_data_t* parallel_data;
  ompt_data_t* task_data;
//  int exists_parallel = ompt_get_parallel_info(level, &parallel_data, NULL);
  int exists_task = ompt_get_task_info(level, NULL, &task_data, &frame, &parallel_data, NULL);
  if (frame)
  {
    if (debug_on) {
      printf("%" PRIu64 ": task level %d: parallel_id=%" PRIu64 ", task_id=%" PRIu64 ", exit_frame=%p, enter_frame=%p\n", ompt_get_thread_data()->value, level, exists_task ? parallel_data->value : 0, exists_task ? task_data->value : 0, frame->exit_frame, frame->enter_frame);
    }
//    printf("%" PRIu64 ": parallel level %d: parallel_id=%" PRIu64 ", task_id=%" PRIu64 ", exit_frame=%p, reenter_frame=%p\n", ompt_get_thread_data()->value, level, exists_parallel ? parallel_data->value : 0, exists_task ? task_data->value : 0, frame->exit_frame, frame->enter_frame);
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

static void
on_ompt_callback_mutex_acquire(
  ompt_mutex_t kind,
  unsigned int hint,
  unsigned int impl,
  omp_wait_id_t wait_id,
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
  omp_wait_id_t wait_id,
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
  omp_wait_id_t wait_id,
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
    omp_wait_id_t wait_id,
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
on_ompt_callback_sync_region(
  ompt_sync_region_t kind,
  ompt_scope_endpoint_t endpoint,
  ompt_data_t *parallel_data,
  ompt_data_t *task_data,
  const void *codeptr_ra)
{
  int thread_id = get_global_thread_num();
#ifdef ENABLE_ENERGY
  if (thread_id == 0) {
    rapl_sysfs_read_packages(package_energy); // Read package energy counters.
  }
#endif
  switch(endpoint)
  {
    case ompt_scope_begin:
      switch(kind)
      {
        case ompt_sync_region_barrier:
          tracepoint(lttng_pinsight, barrier_begin, thread_id, parallel_data, task_data, codeptr_ra
#ifdef ENABLE_ENERGY
                  , package_energy[0], package_energy[1], package_energy[2], package_energy[3]
#endif
                  );
          if (debug_on) {
            printf("%" PRIu64 ": ompt_event_barrier_begin: parallel_id=%" PRIu64 ", task_id=%" PRIu64 ", return_address=%p\n", ompt_get_thread_data()->value, parallel_data->value, task_data->value, codeptr_ra);
	  }
          //print_ids(0);
          break;
        case ompt_sync_region_taskwait:
          tracepoint(lttng_pinsight, taskwait_begin, thread_id, parallel_data, task_data, codeptr_ra
#ifdef ENABLE_ENERGY
              , package_energy[0], package_energy[1], package_energy[2], package_energy[3]
#endif
          );
          break;
        case ompt_sync_region_taskgroup:
          tracepoint(lttng_pinsight, taskgroup_begin, thread_id, parallel_data, task_data, codeptr_ra
#ifdef ENABLE_ENERGY
              , package_energy[0], package_energy[1], package_energy[2], package_energy[3]
#endif
          );
          break;
      }
      break;
    case ompt_scope_end:
      switch(kind)
      {
        case ompt_sync_region_barrier:
          tracepoint(lttng_pinsight, barrier_end, thread_id, parallel_data, task_data, codeptr_ra
#ifdef ENABLE_ENERGY
              , package_energy[0], package_energy[1], package_energy[2], package_energy[3]
#endif
              );
          if (debug_on) {
            printf("%" PRIu64 ": ompt_event_barrier_end: parallel_id=%" PRIu64 ", task_id=%" PRIu64 ", return_address=%p\n", ompt_get_thread_data()->value, (parallel_data)?parallel_data->value:0, task_data->value, codeptr_ra);
	  }
          break;
        case ompt_sync_region_taskwait:
          tracepoint(lttng_pinsight, taskwait_end, thread_id, parallel_data, task_data, codeptr_ra
#ifdef ENABLE_ENERGY
              , package_energy[0], package_energy[1], package_energy[2], package_energy[3]
#endif
              );
          break;
        case ompt_sync_region_taskgroup:
          tracepoint(lttng_pinsight, taskgroup_end, thread_id, parallel_data, task_data, codeptr_ra
#ifdef ENABLE_ENERGY
              , package_energy[0], package_energy[1], package_energy[2], package_energy[3]
#endif
              );
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
  int thread_id = get_global_thread_num();
#ifdef ENABLE_ENERGY
  if (thread_id == 0) {
    rapl_sysfs_read_packages(package_energy); // Read package energy counters.
  }
#endif
  switch(endpoint)
  {
    case ompt_scope_begin:
      switch(kind)
      {
        case ompt_sync_region_barrier:
          tracepoint(lttng_pinsight, barrier_wait_begin, thread_id, parallel_data, task_data, codeptr_ra
#ifdef ENABLE_ENERGY
              , package_energy[0], package_energy[1], package_energy[2], package_energy[3]
#endif
              );
          if (debug_on) {
            printf("%" PRIu64 ": ompt_event_wait_barrier_begin: parallel_id=%" PRIu64 ", task_id=%" PRIu64 ", return_address=%p\n", ompt_get_thread_data()->value, parallel_data->value, task_data->value, codeptr_ra);
	  }
          break;
        case ompt_sync_region_taskwait:
          tracepoint(lttng_pinsight, taskwait_wait_begin, thread_id, parallel_data, task_data, codeptr_ra
#ifdef ENABLE_ENERGY
              , package_energy[0], package_energy[1], package_energy[2], package_energy[3]
#endif
              );
          break;
        case ompt_sync_region_taskgroup:
          tracepoint(lttng_pinsight, taskgroup_wait_begin, thread_id, parallel_data, task_data, codeptr_ra
#ifdef ENABLE_ENERGY
              , package_energy[0], package_energy[1], package_energy[2], package_energy[3]
#endif
              );
          break;
      }
      break;
    case ompt_scope_end:
      switch(kind)
      {
        case ompt_sync_region_barrier:
          tracepoint(lttng_pinsight, barrier_wait_end, thread_id, parallel_data, task_data, codeptr_ra
#ifdef ENABLE_ENERGY
              , package_energy[0], package_energy[1], package_energy[2], package_energy[3]
#endif
              );
          if (debug_on) {
            printf("%" PRIu64 ": ompt_event_wait_barrier_end: parallel_id=%" PRIu64 ", task_id=%" PRIu64 ", return_address=%p\n", ompt_get_thread_data()->value, (parallel_data)?parallel_data->value:0, task_data->value, codeptr_ra);
	  }
          break;
        case ompt_sync_region_taskwait:
          tracepoint(lttng_pinsight, taskwait_wait_end, thread_id, parallel_data, task_data, codeptr_ra
#ifdef ENABLE_ENERGY
              , package_energy[0], package_energy[1], package_energy[2], package_energy[3]
#endif
              );
          break;
        case ompt_sync_region_taskgroup:
          tracepoint(lttng_pinsight, taskgroup_wait_end, thread_id, parallel_data, task_data, codeptr_ra
#ifdef ENABLE_ENERGY
              , package_energy[0], package_energy[1], package_energy[2], package_energy[3]
#endif
              );
          break;
      }
      break;
  }
}

static void
on_ompt_event_control(
  uint64_t command,
  uint64_t modifier)
{
  if (debug_on) {
    printf("%" PRIu64 ": ompt_event_control: command=%" PRIu64 ", modifier=%" PRIu64 "\n", ompt_get_thread_data()->value, command, modifier);
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
//  int thread_id = get_global_thread_num();
//  switch(endpoint)
//  {
//    case ompt_scope_begin:
//      tracepoint(lttng_pinsight, thread_idle_begin, thread_id, ompt_get_thread_data());
//      //printf("%" PRIu64 ": ompt_event_idle_begin:\n", ompt_get_thread_data()->value);
//      //printf("%" PRIu64 ": ompt_event_idle_begin: thread_id=%" PRIu64 "\n", ompt_get_thread_data()->value, thread_data.value);
//      break;
//    case ompt_scope_end:
//      tracepoint(lttng_pinsight, thread_idle_end, thread_id, ompt_get_thread_data());
//      //printf("%" PRIu64 ": ompt_event_idle_end:\n", ompt_get_thread_data()->value);
//      //printf("%" PRIu64 ": ompt_event_idle_end: thread_id=%" PRIu64 "\n", ompt_get_thread_data()->value, thread_data.value);
//      break;
//  }
//}

static void
on_ompt_callback_implicit_task(
    ompt_scope_endpoint_t endpoint,
    ompt_data_t *parallel_data,
    ompt_data_t *task_data,
    unsigned int team_size,
    unsigned int thread_num)
{
  /* in this call back, parallel_data is NULL for ompt_scope_end endpoint, thus to know the parallel_data at the end,
   * we need to pass the needed fields of parallel_data in the scope_begin to the task_data */
  int thread_id = get_global_thread_num();
#ifdef ENABLE_ENERGY
  if (thread_id == 0) {
    rapl_sysfs_read_packages(package_energy); // Read package energy counters.
  }
#endif
  switch(endpoint)
  {
    case ompt_scope_begin:
      task_data->value = ompt_get_unique_id();
      tracepoint(lttng_pinsight, implicit_task_begin, thread_id, parallel_data, task_data, team_size, thread_num
#ifdef ENABLE_ENERGY
          , package_energy[0], package_energy[1], package_energy[2], package_energy[3]
#endif
          );
      if (debug_on) {
        printf("%" PRIu64 ": ompt_event_implicit_task_begin: parallel_id=%" PRIu64 ", task_id=%" PRIu64 ", team_size=%" PRIu32 ", thread_num=%" PRIu32 "\n", ompt_get_thread_data()->value, parallel_data->value, task_data->value, team_size, thread_num);
      }
      break;
    case ompt_scope_end:
      tracepoint(lttng_pinsight, implicit_task_end, thread_id, parallel_data, task_data, team_size, thread_num
#ifdef ENABLE_ENERGY
          , package_energy[0], package_energy[1], package_energy[2], package_energy[3]
#endif
          );
      if (debug_on) {
	printf("%" PRIu64 ": ompt_event_implicit_task_end: parallel_id=%" PRIu64 ", task_id=%" PRIu64 ", team_size=%" PRIu32 ", thread_num=%" PRIu32 "\n", ompt_get_thread_data()->value, (parallel_data)?parallel_data->value:0, task_data->value, team_size, thread_num);
      }
      break;
  }
}

static void
on_ompt_callback_lock_init(
  ompt_mutex_t kind,
  unsigned int hint,
  unsigned int impl,
  omp_wait_id_t wait_id,
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
  omp_wait_id_t wait_id,
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
on_ompt_callback_work(
  ompt_work_t wstype,
  ompt_scope_endpoint_t endpoint,
  ompt_data_t *parallel_data,
  ompt_data_t *task_data,
  uint64_t count,
  const void *codeptr_ra)
{
  int thread_id = get_global_thread_num();
#ifdef ENABLE_ENERGY
  if (thread_id == 0) {
    rapl_sysfs_read_packages(package_energy); // Read package energy counters.
  }
#endif
  switch(endpoint)
  {
    case ompt_scope_begin:
      switch(wstype)
      {
        case ompt_work_loop:
          tracepoint(lttng_pinsight, work_loop_begin, thread_id, parallel_data, task_data, count, codeptr_ra
#ifdef ENABLE_ENERGY
              , package_energy[0], package_energy[1], package_energy[2], package_energy[3]
#endif
              );
          if (debug_on) {
            printf("%" PRIu64 ": ompt_event_loop_begin: parallel_id=%" PRIu64 ", parent_task_id=%" PRIu64 ", workshare_function=%p, count=%" PRIu64 "\n", ompt_get_thread_data()->value, parallel_data->value, task_data->value, codeptr_ra, count);
	  }
          break;
        case ompt_work_sections:
          tracepoint(lttng_pinsight, work_sections_begin, thread_id, parallel_data, task_data, count, codeptr_ra
#ifdef ENABLE_ENERGY
              , package_energy[0], package_energy[1], package_energy[2], package_energy[3]
#endif
              );
          //impl
          break;
        case ompt_work_single_executor:
          tracepoint(lttng_pinsight, work_single_executor_begin, thread_id, parallel_data, task_data, count, codeptr_ra
#ifdef ENABLE_ENERGY
              , package_energy[0], package_energy[1], package_energy[2], package_energy[3]
#endif
              );
          if (debug_on) {
            printf("%" PRIu64 ": ompt_event_single_in_block_begin: parallel_id=%" PRIu64 ", parent_task_id=%" PRIu64 ", workshare_function=%p, count=%" PRIu64 "\n", ompt_get_thread_data()->value, parallel_data->value, task_data->value, codeptr_ra, count);
	  }
          break;
        case ompt_work_single_other:
          tracepoint(lttng_pinsight, work_single_other_begin, thread_id, parallel_data, task_data, count, codeptr_ra
#ifdef ENABLE_ENERGY
              , package_energy[0], package_energy[1], package_energy[2], package_energy[3]
#endif
              );
          if (debug_on) {
            printf("%" PRIu64 ": ompt_event_single_others_begin: parallel_id=%" PRIu64 ", task_id=%" PRIu64 ", workshare_function=%p, count=%" PRIu64 "\n", ompt_get_thread_data()->value, parallel_data->value, task_data->value, codeptr_ra, count);
	  }
          break;
        case ompt_work_workshare:
          tracepoint(lttng_pinsight, work_workshare_begin, thread_id, parallel_data, task_data, count, codeptr_ra
#ifdef ENABLE_ENERGY
              , package_energy[0], package_energy[1], package_energy[2], package_energy[3]
#endif
              );
          //impl
          break;
        case ompt_work_distribute:
          tracepoint(lttng_pinsight, work_distribute_begin, thread_id, parallel_data, task_data, count, codeptr_ra
#ifdef ENABLE_ENERGY
              , package_energy[0], package_energy[1], package_energy[2], package_energy[3]
#endif
              );
          //impl
          break;
        case ompt_work_taskloop:
          tracepoint(lttng_pinsight, work_taskloop_begin, thread_id, parallel_data, task_data, count, codeptr_ra
#ifdef ENABLE_ENERGY
              , package_energy[0], package_energy[1], package_energy[2], package_energy[3]
#endif
              );
          //impl
          break;
      }
      break;
    case ompt_scope_end:
      switch(wstype)
      {
        case ompt_work_loop:
          tracepoint(lttng_pinsight, work_loop_end, thread_id, parallel_data, task_data, count, codeptr_ra
#ifdef ENABLE_ENERGY
              , package_energy[0], package_energy[1], package_energy[2], package_energy[3]
#endif
              );
          if (debug_on) {
            printf("%" PRIu64 ": ompt_event_loop_end: parallel_id=%" PRIu64 ", task_id=%" PRIu64 ", workshare_function=%p, count=%" PRIu64 "\n", ompt_get_thread_data()->value, parallel_data->value, task_data->value, codeptr_ra, count);
	  }
          break;
        case ompt_work_sections:
          tracepoint(lttng_pinsight, work_sections_end, thread_id, parallel_data, task_data, count, codeptr_ra
#ifdef ENABLE_ENERGY
              , package_energy[0], package_energy[1], package_energy[2], package_energy[3]
#endif
              );
          //impl
          break;
        case ompt_work_single_executor:
          tracepoint(lttng_pinsight, work_single_executor_end, thread_id, parallel_data, task_data, count, codeptr_ra
#ifdef ENABLE_ENERGY
              , package_energy[0], package_energy[1], package_energy[2], package_energy[3]
#endif
              );
          if (debug_on) {
            printf("%" PRIu64 ": ompt_event_single_in_block_end: parallel_id=%" PRIu64 ", task_id=%" PRIu64 ", workshare_function=%p, count=%" PRIu64 "\n", ompt_get_thread_data()->value, parallel_data->value, task_data->value, codeptr_ra, count);
	  }
          break;
        case ompt_work_single_other:
          tracepoint(lttng_pinsight, work_single_other_end, thread_id, parallel_data, task_data, count, codeptr_ra
#ifdef ENABLE_ENERGY
              , package_energy[0], package_energy[1], package_energy[2], package_energy[3]
#endif
              );
          if (debug_on) {
            printf("%" PRIu64 ": ompt_event_single_others_end: parallel_id=%" PRIu64 ", task_id=%" PRIu64 ", workshare_function=%p, count=%" PRIu64 "\n", ompt_get_thread_data()->value, parallel_data->value, task_data->value, codeptr_ra, count);
	  }
          break;
        case ompt_work_workshare:
          tracepoint(lttng_pinsight, work_workshare_end, thread_id, parallel_data, task_data, count, codeptr_ra
#ifdef ENABLE_ENERGY
              , package_energy[0], package_energy[1], package_energy[2], package_energy[3]
#endif
              );
          //impl
          break;
        case ompt_work_distribute:
          tracepoint(lttng_pinsight, work_distribute_end, thread_id, parallel_data, task_data, count, codeptr_ra
#ifdef ENABLE_ENERGY
              , package_energy[0], package_energy[1], package_energy[2], package_energy[3]
#endif
              );
          //impl
          break;
        case ompt_work_taskloop:
          tracepoint(lttng_pinsight, work_taskloop_end, thread_id, parallel_data, task_data, count, codeptr_ra
#ifdef ENABLE_ENERGY
              , package_energy[0], package_energy[1], package_energy[2], package_energy[3]
#endif
              );
          //impl
          break;
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
  int thread_id = get_global_thread_num();
#ifdef ENABLE_ENERGY
  if (thread_id == 0) {
    rapl_sysfs_read_packages(package_energy); // Read package energy counters.
  }
#endif
  switch(endpoint)
  {
    case ompt_scope_begin:
      tracepoint(lttng_pinsight, master_begin, thread_id, parallel_data, task_data, codeptr_ra
#ifdef ENABLE_ENERGY
          , package_energy[0], package_energy[1], package_energy[2], package_energy[3]
#endif
          );
      if (debug_on) {
        printf("%" PRIu64 ": ompt_event_master_begin: parallel_id=%" PRIu64 ", task_id=%" PRIu64 ", codeptr_ra=%p\n", ompt_get_thread_data()->value, parallel_data->value, task_data->value, codeptr_ra);
      }
      break;
    case ompt_scope_end:
      tracepoint(lttng_pinsight, master_end, thread_id, parallel_data, task_data, codeptr_ra
#ifdef ENABLE_ENERGY
          , package_energy[0], package_energy[1], package_energy[2], package_energy[3]
#endif
          );
      if (debug_on) {
        printf("%" PRIu64 ": ompt_event_master_end: parallel_id=%" PRIu64 ", task_id=%" PRIu64 ", codeptr_ra=%p\n", ompt_get_thread_data()->value, parallel_data->value, task_data->value, codeptr_ra);
      }
      break;
  }
}

// TODO: Add printf for `flag` parameter.
static void
on_ompt_callback_parallel_begin(
  ompt_data_t *parent_task_data,
  const omp_frame_t *parent_task_frame,
  ompt_data_t* parallel_data,
  uint32_t requested_parallelism,
  int32_t flag,
//  uint32_t actual_team_size,
  const void *codeptr_ra)
{
  parallel_data->value = ompt_get_unique_id();
  int thread_id = get_global_thread_num();
#ifdef ENABLE_ENERGY
  if (thread_id == 0) {
    rapl_sysfs_read_packages(package_energy); // Read package energy counters.
  }
#endif
  tracepoint(lttng_pinsight, parallel_begin, thread_id, parent_task_frame, parallel_data, requested_parallelism, codeptr_ra
#ifdef ENABLE_ENERGY
  , package_energy[0], package_energy[1], package_energy[2], package_energy[3]
#endif
  );
  if (debug_on) {
    printf("%" PRIu64 ": ompt_event_parallel_begin: parent_task_id=%" PRIu64 ", parent_task_frame.exit=%p, parent_task_frame.reenter=%p, parallel_id=%" PRIu64 ", requested_parallelism=%" PRIu32 ", parallel_function=%p\n", ompt_get_thread_data()->value, parent_task_data->value, parent_task_frame->exit_frame, parent_task_frame->enter_frame, parallel_data->value, requested_parallelism, codeptr_ra);
  }
}

// TODO: Add printf for `flag` parameter.
static void
on_ompt_callback_parallel_end(
  ompt_data_t *parallel_data,
  ompt_data_t *task_data,
  int32_t flag,
  const void *codeptr_ra)
{
  int thread_id = get_global_thread_num();
#ifdef ENABLE_ENERGY
  if (thread_id == 0) {
    rapl_sysfs_read_packages(package_energy); // Read package energy counters.
  }
#endif
  tracepoint(lttng_pinsight, parallel_end, thread_id, parallel_data, task_data, codeptr_ra
#ifdef ENABLE_ENERGY
  , package_energy[0], package_energy[1], package_energy[2], package_energy[3]
#endif
  );
  if (debug_on) {
    printf("%" PRIu64 ": ompt_event_parallel_end: parallel_id=%" PRIu64 ", task_id=%" PRIu64 ", codeptr_ra=%p\n", ompt_get_thread_data()->value, parallel_data->value, task_data->value, codeptr_ra);
  }
}

// TODO: Add printf for `flag` parameter.
static void
on_ompt_callback_task_create(
    ompt_data_t *parent_task_data,    /* id of parent task            */
    const omp_frame_t *parent_frame,  /* frame data for parent task   */
    ompt_data_t* new_task_data,      /* id of created task           */
    int flag,
    int has_dependences,
    const void *codeptr_ra)               /* pointer to outlined function */
{
  new_task_data->value = ompt_get_unique_id();
  if (debug_on) {
    printf("%" PRIu64 ": ompt_event_task_create: parent_task_id=%" PRIu64 ", parent_task_frame.exit=%p, parent_task_frame.enter=%p, new_task_id=%" PRIu64 ", parallel_function=%p, has_dependences=%s\n", ompt_get_thread_data()->value, parent_task_data ? parent_task_data->value : 0, parent_frame ? parent_frame->exit_frame : NULL, parent_frame ? parent_frame->enter_frame : NULL, new_task_data->value, codeptr_ra, has_dependences ? "yes" : "no");
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

static void
on_ompt_callback_thread_begin(
  ompt_thread_t thread_type,
  ompt_data_t *thread_data)
{
  int thread_id = get_global_thread_num();
  thread_data->value = ompt_get_unique_id();
#ifdef ENABLE_ENERGY
  if (thread_id == 0) {
    rapl_sysfs_read_packages(package_energy); // Read package energy counters.
  }
#endif
  tracepoint(lttng_pinsight, thread_begin, thread_id, thread_data
#ifdef ENABLE_ENERGY
  , package_energy[0], package_energy[1], package_energy[2], package_energy[3]
#endif
  );
  if (debug_on) {
    printf("%" PRIu64 ": ompt_event_thread_begin: thread_type=%s=%d, thread_id=%" PRIu64 "\n", ompt_get_thread_data()->value, ompt_thread_type_t_values[thread_type], thread_type, thread_data->value);
  }
}

static void
on_ompt_callback_thread_end(
  ompt_data_t *thread_data)
{
  int thread_id = get_global_thread_num();
#ifdef ENABLE_ENERGY
  if (thread_id == 0) {
    rapl_sysfs_read_packages(package_energy); // Read package energy counters.
  }
#endif
  tracepoint(lttng_pinsight, thread_end, thread_id, thread_data
#ifdef ENABLE_ENERGY
  , package_energy[0], package_energy[1], package_energy[2], package_energy[3]
#endif
  );
  if (debug_on) {
    printf("%" PRIu64 ": ompt_event_thread_end: thread_id=%" PRIu64 "\n", ompt_get_thread_data()->value, thread_data->value);
    //printf("%" PRIu64 ": ompt_event_thread_end: thread_type=%s=%d, thread_id=%" PRIu64 "\n", ompt_get_thread_data()->value, ompt_thread_type_t_values[thread_type], thread_type, thread_data->value);
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

  register_callback(ompt_callback_mutex_acquire);
  register_callback_t(ompt_callback_mutex_acquired, ompt_callback_mutex_t);
  register_callback_t(ompt_callback_mutex_released, ompt_callback_mutex_t);
  register_callback(ompt_callback_nest_lock);
  register_callback(ompt_callback_sync_region);
  register_callback_t(ompt_callback_sync_region_wait, ompt_callback_sync_region_t);
  register_callback(ompt_callback_control_tool);
  register_callback(ompt_callback_flush);
  register_callback(ompt_callback_cancel);
  //register_callback(ompt_callback_idle);  // Note: Obsoleted in TR7, as it was weird/impossible to implement correctly.
  register_callback(ompt_callback_implicit_task);
  register_callback_t(ompt_callback_lock_init, ompt_callback_mutex_acquire_t);
  register_callback_t(ompt_callback_lock_destroy, ompt_callback_mutex_t);
  register_callback(ompt_callback_work);
  register_callback(ompt_callback_master);
  register_callback(ompt_callback_parallel_begin);
  register_callback(ompt_callback_parallel_end);
  register_callback(ompt_callback_task_create);
  register_callback(ompt_callback_task_schedule);
  register_callback(ompt_callback_task_dependences);
  register_callback(ompt_callback_task_dependence);
  register_callback(ompt_callback_thread_begin);
  register_callback(ompt_callback_thread_end);

  // Query environment variables to enable/dsiable debug printouts.
  debug_on = env_get_long(PINSIGHT_DEBUG_ENABLE, PINSIGHT_DEBUG_ENABLE_DEFAULT);
  if (debug_on) {
    printf("0: NULL_POINTER=%p\n", NULL);
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
