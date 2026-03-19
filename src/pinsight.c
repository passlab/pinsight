#include "pinsight.h"
#include "trace_config.h"
#include <stdio.h>
#include <stdlib.h>

__thread pinsight_thread_data_t pinsight_thread_data;

volatile sig_atomic_t mode_change_requested = 0;

/**
 * Fire auto-trigger mode changes when a lexgion reaches max_num_traces.
 * Iterates over mode_after[] for all domains; PINSIGHT_DOMAIN_NONE means
 * no change requested for that domain.
 * Sets mode_change_requested flag to defer callback re-registration.
 */
void pinsight_fire_mode_triggers(lexgion_trace_config_t *tc) {
  for (int d = 0; d < num_domain; d++) {
    pinsight_domain_mode_t new_mode = tc->mode_after[d];
    if (new_mode == PINSIGHT_DOMAIN_NONE)
      continue;
    if (!domain_default_trace_config[d].mode_change_fired) {
      domain_default_trace_config[d].mode = new_mode;
      domain_default_trace_config[d].mode_change_fired = 1;
      fprintf(stderr, "PInsight: Auto-trigger: %s mode -> %s\n",
              domain_info_table[d].name,
              new_mode == PINSIGHT_DOMAIN_OFF          ? "OFF"
              : new_mode == PINSIGHT_DOMAIN_MONITORING ? "MONITORING"
                                                       : "TRACING");
    }
  }
  // Defer callback re-registration to next top-level callback entry
  mode_change_requested = 1;
}

/** init thread data
 */
pinsight_thread_data_t *init_thread_data(int _thread_num) {
  global_thread_num = _thread_num;
  //    pinsight_thread_data.thread_type = thread_type;
  pinsight_thread_data.stack_top = -1;
  pinsight_thread_data.current_record = NULL;
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
lexgion_record_t *push_lexgion(lexgion_t *lgp, unsigned int record_id) {
  int top = pinsight_thread_data.stack_top + 1;
  if (top == MAX_LEXGION_STACK_DEPTH) {
    fprintf(stderr, "thread %d lexgion stack overflow\n", global_thread_num);
    return NULL;
  }
  lexgion_record_t *record = &pinsight_thread_data.lexgion_stack[top];
  record->lgp = lgp;
  record->record_id = record_id;
  record->parent = pinsight_thread_data.current_record;
  pinsight_thread_data.current_record = record;
  pinsight_thread_data.stack_top = top;

  return record;
}

/**
 * pop the lexgion out of the stack and also return the record_id if it is
 * requested.
 * @param record_id
 * @return
 */
lexgion_t *pop_lexgion(unsigned int *record_id) {
  lexgion_record_t *record = pinsight_thread_data.current_record;
  if (record == NULL)
    return NULL;
  lexgion_t *lgp = record->lgp;
  if (record_id != NULL)
    *record_id = record->record_id;
  pinsight_thread_data.current_record = record->parent;
  pinsight_thread_data.stack_top--;
  return lgp;
}

/**
 * this can be called by multiple threads since it is a read-only search.
 * @param codeptr_ra
 * @param index
 * @return
 */
static lexgion_t *find_lexgion(int class, int type, const void *codeptr_ra,
                               int *index) {
  /* play it safe for dealing with data race */
  if (pinsight_thread_data.recent_lexgion < 0 ||
      pinsight_thread_data.num_lexgions <= 0)
    return NULL;
  int i;
  lexgion_t *lgp;

  /* search forward from the most recent one */
  for (i = pinsight_thread_data.recent_lexgion;
       i < pinsight_thread_data.num_lexgions; i++) {
    if (class == pinsight_thread_data.lexgions[i].class &&
        type == pinsight_thread_data.lexgions[i].type &&
        pinsight_thread_data.lexgions[i].codeptr_ra == codeptr_ra) {
      *index = i;
      lgp = &pinsight_thread_data.lexgions[i];
      return lgp;
    }
  }
  /* search from 0 to most recent one */
  for (i = 0; i < pinsight_thread_data.recent_lexgion; i++) {
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
    fprintf(stderr,
            "FATAL: Max number of lexgions (%d) allowed in the source code "
            "reached, cannot continue\n",
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
    lgp->type = type;
    lgp->class = class;

    /* init counters for number of exes, traces, and sampling */
    lgp->counter = 0;
    lgp->trace_counter = 0;
    lgp->num_exes_after_last_trace = 0;
    lgp->trace_config =
        NULL; /* resolved lazily by lexgion_set_top_trace_bit_domain_event */
    lgp->trace_config_change_counter =
        -1; /* force mismatch to trigger config resolution */
  }
  pinsight_thread_data.recent_lexgion = index; /* cache it for future search */

  lgp->num_exes_after_last_trace++;
  lgp->counter++; // counter only increment
  if (lgp->counter >= 0xFFFF) {
    // fprintf(stderr, "FATAL: Trace record overflow, more than 2^^16 traces
    // (%d) would be recorded for lexgion: %p\n", lgp->counter, codeptr_ra);
  }

  return push_lexgion(lgp, lgp->counter);
}

/**
 * mark the end of a lexgion, return the lexgion and the record_id of the
 * instance if it is requested
 * @param record_id: record_id of the lexgion instance that is just ended.
 * @return
 */
lexgion_t *lexgion_end(unsigned int *record_id) {
  return pop_lexgion(record_id);
}

/**
 * search the lexgion stack to find the topmost lexgion record in the stack of
 * the specified type.
 * @param type
 * @return
 */
lexgion_record_t *top_lexgion_type(int class, int type) {
  lexgion_record_t *record = pinsight_thread_data.current_record;
  while (record != NULL) {
    lexgion_t *lgp = record->lgp;
    if (lgp->class == class && lgp->type == type) {
      return record;
    }
    record = record->parent;
  }
  return NULL;
}

lexgion_record_t *top_lexgion() { return pinsight_thread_data.current_record; }

/**
 * Set the trace bit for a lexgion based on domain and event, with 3-level
 * config resolution:
 *   1. Search lexgion_address_trace_config by lgp->codeptr_ra
 *   2. Fall back to lexgion_domain_default_trace_config[domain]
 *   3. Fall back to lexgion_default_trace_config
 * @return 1 if the lexgion should be traced, 0 otherwise
 */
int lexgion_set_top_trace_bit_domain_event(lexgion_t *lgp, int domain,
                                           int event) {

  /* SIGUSR1 config reload is deferred to the sequential post-join point
   * in on_ompt_callback_parallel_end, next to mode_change_requested.
   * This avoids data races from reloading config or re-registering
   * callbacks while other threads are in-flight. */

  /* Auto-trigger mode changes are deferred to the sequential path:
   * on_ompt_callback_parallel_end (after join) in ompt_callback.c
   * checks mode_change_requested and calls
   * pinsight_register_openmp_callbacks() when safe. */

  lexgion_trace_config_t *trace_config = lgp->trace_config;

  /* Re-resolve config if not yet set or if a reconfig has occurred since last
   * resolution */
  if (trace_config == NULL ||
      lgp->trace_config_change_counter != trace_config_change_counter) {
    trace_config = NULL;

    /* 1. Search the lexgion_address_trace_config table by lgp->codeptr_ra */
    for (int i = 0; i < num_lexgion_address_trace_configs; i++) {
      if (lexgion_address_trace_config[i].codeptr == lgp->codeptr_ra &&
          !lexgion_address_trace_config[i].removed) {
        trace_config = &lexgion_address_trace_config[i];
        break;
      }
    }

    /* 2. If not found, try the domain-specific default config */
    if (trace_config == NULL) {
      lexgion_trace_config_t *domain_default =
          &lexgion_domain_default_trace_config[domain];
      if (domain_default->codeptr != NULL) {
        trace_config = domain_default;
      }
    }

    /* 3. If still not found, fall back to the global default */
    if (trace_config == NULL) {
      trace_config = lexgion_default_trace_config;
    }

    /* Cache the resolved config and current generation */
    lgp->trace_config = trace_config;
    lgp->trace_config_change_counter = trace_config_change_counter;
  }

  /* Check whether the current domain event is enabled for tracing */
  if (trace_config->domain_events[domain].set &&
      !((trace_config->domain_events[domain].events >> event) & 1)) {
    lgp->trace_bit = 0;
    return 0;
  }

  /* Check whether the punit set is enabled for tracing */
  if (trace_config->domain_punit_set_set) {
    if (!domain_punit_set_match(trace_config->domain_punits)) {
      lgp->trace_bit = 0;
      return 0;
    }
  }

  /* Compute the rate-based trace bit */
  lgp->trace_bit =
      (trace_config->trace_starts_at <= (lgp->counter - 1) &&
       ((trace_config->max_num_traces == -1) ||
        (lgp->trace_counter < trace_config->max_num_traces)) &&
       (lgp->num_exes_after_last_trace >= trace_config->tracing_rate));
  return lgp->trace_bit;
}

/**
 * Set the trace config for a lexgion by checking the
 * lexgion_address_trace_config table, the domain_default_trace_config table,
 * and the default_trace_config table.
 * @return the trace config for the lexgion
 */
lexgion_trace_config_t *lexgion_set_trace_config(lexgion_t *lgp, int domain) {
  lexgion_trace_config_t *trace_config = lgp->trace_config;
  /* Re-resolve config if not yet set or if a reconfig has occurred since last
   * resolution */
  if (trace_config == NULL ||
      lgp->trace_config_change_counter != trace_config_change_counter) {
    trace_config = NULL;

    /* 1. Search the lexgion_address_trace_config table by lgp->codeptr_ra */
    for (int i = 0; i < num_lexgion_address_trace_configs; i++) {
      if (lexgion_address_trace_config[i].codeptr == lgp->codeptr_ra &&
          !lexgion_address_trace_config[i].removed) {
        trace_config = &lexgion_address_trace_config[i];
        break;
      }
    }

    /* 2. If not found, try the domain-specific default config */
    if (trace_config == NULL) {
      lexgion_trace_config_t *domain_default =
          &lexgion_domain_default_trace_config[domain];
      if (domain_default->codeptr != NULL) {
        trace_config = domain_default;
      }
    }

    /* 3. If still not found, fall back to the global default */
    if (trace_config == NULL) {
      trace_config = lexgion_default_trace_config;
    }

    /* Cache the resolved config and current generation */
    lgp->trace_config = trace_config;
    lgp->trace_config_change_counter = trace_config_change_counter;
  }
  return trace_config;
}

/**
 * Set the trace bit for a lexgion based on the rate configuration.
 * @return 1 if the event should be traced, 0 otherwise
 */
int lexgion_set_rate_trace_bit(lexgion_t *lgp) {
  lexgion_trace_config_t *trace_config = lgp->trace_config;
  lgp->trace_bit =
      (trace_config->trace_starts_at <= (lgp->counter - 1) &&
       ((trace_config->max_num_traces == -1) ||
        (lgp->trace_counter < trace_config->max_num_traces)) &&
       (lgp->num_exes_after_last_trace >= trace_config->tracing_rate));
  return lgp->trace_bit;
}

/**
 * Check if a specific domain/event is enabled for a lexgion or not, it also
 * checks the mactching of the punit set.
 * @return 1 if the event should be traced, 0 otherwise
 */
int lexgion_check_event_enabled(lexgion_t *lgp, int domain, int event) {
  lexgion_trace_config_t *tc = lgp->trace_config;
  if (tc != NULL && tc->domain_events[domain].set &&
      !((tc->domain_events[domain].events >> event) & 1)) {
    return 0;
  }

  /* Check whether the punit set is enabled for tracing */
  if (tc->domain_punit_set_set) {
    if (!domain_punit_set_match(tc->domain_punits)) {
      return 0;
    }
  }

  return 1;
}
