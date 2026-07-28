#include "app_knob.h"
#include "trace_config.h"
#include "pinsight_config.h"
#include <ctype.h>
#include <dlfcn.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <stdint.h>
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#include <strings.h>
#ifdef PINSIGHT_MPI
#include "trace_domain_MPI.h"
#endif

#ifdef PINSIGHT_OPENMP
#include "trace_domain_OpenMP.h"
#endif

#ifdef PINSIGHT_CUDA
#include "trace_domain_CUDA.h"
#endif

#ifdef PINSIGHT_PYTHON
#include "trace_domain_Python.h"
#endif

#ifdef PINSIGHT_HIP
#include "trace_domain_HIP.h"
#endif

#ifdef PINSIGHT_LAYOUT_PAD
/* Layout-perturbation probe — see roctracer_callback.c twin. Shifts the
 * hot per-event-read config arrays below by PINSIGHT_LAYOUT_PAD bytes. */
__attribute__((used)) static char pinsight_layout_pad_cfg[PINSIGHT_LAYOUT_PAD] = {1};
#endif
struct domain_info domain_info_table[MAX_NUM_DOMAINS];
domain_trace_config_t domain_default_trace_config[MAX_NUM_DOMAINS];
punit_trace_config_t *domain_punit_trace_config[MAX_NUM_DOMAINS];
int num_domain = 0;

lexgion_trace_config_t
    all_lexgion_trace_config[MAX_NUM_DOMAINS + MAX_NUM_LEXGIONS + 1];
lexgion_trace_config_t *lexgion_default_trace_config =
    &all_lexgion_trace_config[0];
lexgion_trace_config_t *lexgion_domain_default_trace_config =
    &all_lexgion_trace_config[1];
lexgion_trace_config_t *lexgion_trace_config =
    &all_lexgion_trace_config[1 + MAX_NUM_DOMAINS];
int num_lexgion_trace_configs = 0;
unsigned int trace_config_change_counter = 0;

#ifdef PINSIGHT_CUDA
static inline int pinsight_cuda_runtime_available(void);
#endif
#ifdef PINSIGHT_HIP
static inline int pinsight_hip_runtime_available(void);
#endif

/**
 * Check whether the current execution punit id's are in the punit id set or not
 * of the domain_punit_set
 * @param domain_punit_set: the pointer to the domain_punit_set of all domains
 * (not just a single domain)
 * @return 1 if the current execution punit id's are in the punit id set or not
 * of the domain_punit_set, 0 otherwise
 */
int domain_punit_set_match(domain_punit_set_t *domain_punit_set) {
  int i;
  int match = 0;
  for (i = 0; i < num_domain; i++) {
    if (!domain_punit_set[i].set)
      continue;
    // check whether the current execution punit id is in the punit id set
    struct domain_info *d = &domain_info_table[i];
    domain_punit_set_t *dpst = &domain_punit_set[i];
    int k;
    for (k = 0; k < d->num_punits; k++) {
      if (!dpst->punit[k].set)
        continue; // this punit kind is not constrained in this trace config
      int punit_id;
      if (d->punits[k].num_arg == 0) {
        punit_id = d->punits[k].punit_id_func.func0();
      } else {
        punit_id = d->punits[k].punit_id_func.func1(d->punits[k].arg);
      }
      if (punit_id < d->punits[k].low || punit_id > d->punits[k].high ||
          !bitset_test(&dpst->punit[k].punit_ids, (size_t)punit_id)) {
        return 0;
      } else
        match = 1;
    }
  }
  return match;
}

/**
 * Given a codeptr, lookup a config struct object
 * @param codeptr the pointer to the codeptr
 * @return the pointer to the config struct object
 */
lexgion_trace_config_t *retrieve_lexgion_trace_config(const void *codeptr) {
  for (int i = 0; i < num_lexgion_trace_configs; i++) {
    lexgion_trace_config_t *config = &lexgion_trace_config[i];
    if (config->codeptr == codeptr) {
      if (config->removed) {
        return NULL;
      } else {
        return config;
      }
    }
  }
  return NULL;
}

void setup_trace_config_env() {
  // 1. Override Domain Defaults
  for (int i = 0; i < num_domain; i++) {
    char env_var[256];
    struct domain_info *d = &domain_info_table[i];

    // Construct PINSIGHT_TRACE_<DOMAIN>
    snprintf(env_var, sizeof(env_var), "PINSIGHT_TRACE_%s", d->name);
    // Convert to uppercase "PINSIGHT_TRACE_" is 15 characters; thus start with
    // 15
    for (int j = 15; env_var[j]; j++)
      env_var[j] = toupper((unsigned char)env_var[j]);

    char *val = getenv(env_var);
    if (val) {
      if (strcasecmp(val, "OFF") == 0 || strcasecmp(val, "FALSE") == 0 ||
          strcmp(val, "0") == 0) {
        domain_default_trace_config[i].mode = PINSIGHT_DOMAIN_OFF;
      } else if (strcasecmp(val, "STANDBY") == 0) {
        domain_default_trace_config[i].mode = PINSIGHT_DOMAIN_STANDBY;
      } else if (strcasecmp(val, "MONITORING") == 0 ||
                 strcasecmp(val, "MONITOR") == 0) {
        domain_default_trace_config[i].mode = PINSIGHT_DOMAIN_MONITORING;
      } else {
        /* ON, TRACING, TRUE, 1, or any unrecognized → full tracing */
        domain_default_trace_config[i].mode = PINSIGHT_DOMAIN_TRACING;
      }
    }
  }

  // 2. Override Lexgion Window
  // PINSIGHT_TRACE_WINDOW=start:max:rate:window_timeout[:end_action_string]
  //   window_timeout = wall-clock seconds ending the window (0 = disabled)
  // end_action_string can be:
  //   MONITORING | OpenMP:MONITORING | OpenMP:MONITORING,MPI:OFF
  //   INTROSPECT:60:script.sh[:TRACING]
  // PINSIGHT_TRACE_RATE is retained as a DEPRECATED ALIAS (same grammar).
  char *win_env = getenv("PINSIGHT_TRACE_WINDOW");
  if (!win_env) {
    win_env = getenv("PINSIGHT_TRACE_RATE"); /* deprecated alias */
    if (win_env)
      fprintf(stderr, "PInsight WARNING: PINSIGHT_TRACE_RATE is deprecated; "
                      "use PINSIGHT_TRACE_WINDOW (same grammar)\n");
  }
  if (win_env) {
    // Parse first 4 numeric fields separated by ':'
    int start = 0, max = 0, rate = 0, window = 0;
    int count = sscanf(win_env, "%d:%d:%d:%d", &start, &max, &rate, &window);
    if (count >= 1)
      lexgion_default_trace_config->trace_starts_at = start;
    if (count >= 2)
      lexgion_default_trace_config->max_num_traces = max;
    if (count >= 3)
      lexgion_default_trace_config->tracing_rate = rate;
    if (count >= 4)
      lexgion_default_trace_config->end_action.window_timeout_sec = window;

    // Find the 5th field (end_action string): skip past the 4th ':'
    char *p = (char *)win_env;
    int colons = 0;
    while (*p && colons < 4) {
      if (*p == ':')
        colons++;
      p++;
    }
    // p now points to the start of the end_action string (or '\0')
    if (colons == 4 && *p) {
      parse_window_end_action(p, &lexgion_default_trace_config->end_action);
    }
  }
}

static time_t last_config_mtime = 0;

/* Force the next pinsight_load_trace_config call to re-parse even if mtime is
 * unchanged (e.g. SIGUSR1-triggered reload where file was overwritten in-place
 * within the same second). */
void pinsight_invalidate_config_mtime(void) {
    last_config_mtime = 0;
}

/* Signal handler and config_reload_requested have been moved to
 * pinsight_control_thread.c. The control thread now handles all
 * config reloading and mode switching centrally. */

void pinsight_load_trace_config(char *filepath) {
  if (!filepath) {
    filepath = getenv("PINSIGHT_TRACE_CONFIG_FILE");
  }

  // Fallback: check default config file in current working directory
  int using_fallback = 0;
  if (!filepath) {
    filepath = "pinsight_trace_config.txt";
    using_fallback = 1;
  }

  struct stat st;
  if (stat(filepath, &st) != 0) {
    // Only warn if the user explicitly specified the file
    if (!using_fallback) {
      fprintf(stderr, "WARNING: Cannot stat config file '%s': %s\n", filepath,
              strerror(errno));
    }
  } else if (st.st_mtime != last_config_mtime) {
    last_config_mtime = st.st_mtime;
    // Reset domain-default codeptr so the parser can eagerly re-initialize
    // [Lexgion(Domain).default] entries.  Domains without an explicit section
    // in the config file will remain NULL and be auto-filled below.
    for (int i = 0; i < num_domain; i++) {
      lexgion_domain_default_trace_config[i].codeptr = NULL;
    }
    /* Invalidate named lexgion configs before re-parsing. Named entries have
     * name[0]!='\0' and codeptr==NULL. Marking them removed here ensures stale
     * bindings are not used after reload. The name_resolved_gen mismatch caused
     * by trace_config_change_counter++ below drives each callback to re-set
     * lgp->name, which then re-matches against the freshly parsed named entries. */
    for (int i = 0; i < num_lexgion_trace_configs; i++) {
      if (lexgion_trace_config[i].name[0])
        lexgion_trace_config[i].removed = 1;
    }
    parse_trace_config_file(filepath);

    // Fill domain defaults: combine global lexgion default (rate triple) with
    // each domain's default event config for domains not explicitly configured
    // by the user via [Lexgion(Domain).default] sections.
    for (int i = 0; i < num_domain; i++) {
      lexgion_trace_config_t *dlg = &lexgion_domain_default_trace_config[i];
      if (dlg->codeptr != NULL) {
        // User provided a [Lexgion(Domain).default] for this domain
        // during parsing; do not overwrite.
        continue;
      }
      // Combine global lexgion default with domain event config
      *dlg = *lexgion_default_trace_config;
      // Set non-NULL marker (convention: domain index + 1)
      dlg->codeptr = (void *)(uintptr_t)(i + 1);
      // Merge this domain's default event config
      dlg->domain_events[i].set = 1;
      dlg->domain_events[i].events = domain_default_trace_config[i].events;
    }

    trace_config_change_counter++; // Bump counter so threads re-resolve
                                   // cached trace_config pointers
  }
}

void initial_setup_trace_config() {
#ifdef PINSIGHT_OPENMP
  register_OpenMP_trace_domain();
  // OpenMP support is initialized by ompt_start_tool() callback that is
  // implemented in ompt_callback.c, thus we do not need to initialize here.
#endif
#ifdef PINSIGHT_MPI
  register_MPI_trace_domain();
#endif
#ifdef PINSIGHT_CUDA
  if (pinsight_cuda_runtime_available()) {
    register_CUDA_trace_domain();
  }
#endif
#ifdef PINSIGHT_PYTHON
  register_Python_trace_domain();
#endif
#ifdef PINSIGHT_HIP
  if (pinsight_hip_runtime_available()) {
    register_HIP_trace_domain();
  }
#endif

  // Initialize the default domain trace configs by copying from
  // domain_info_table that has the installed events and starting mode
  int i;
  for (i = 0; i < num_domain; i++) {
    domain_default_trace_config[i].events =
        domain_info_table[i].eventInstallStatus;
    domain_default_trace_config[i].mode_change_fired = 0;
    domain_default_trace_config[i].mode = domain_info_table[i].starting_mode;
    domain_default_trace_config[i].last_mode = PINSIGHT_DOMAIN_NONE;
    // Seed node-policy values from the DSL-declared defaults ([Domain.default]
    // parsing overrides them).
    for (int k = 0; k < domain_info_table[i].num_nodepolicy_keys; k++) {
      domain_default_trace_config[i].nodepolicy[k].policy =
          domain_info_table[i].nodepolicy_keys[k].dflt;
      domain_default_trace_config[i].nodepolicy[k].param = 0;
    }
  }

  // Initialize the default lexgion trace config
  lexgion_default_trace_config->codeptr = NULL;
  lexgion_default_trace_config->tracing_rate =
      DEFAULT_TRACE_RATE; // trace every execution
  lexgion_default_trace_config->trace_starts_at =
      DEFAULT_TRACE_START; // start tracing from the first execution
  lexgion_default_trace_config->max_num_traces =
      DEFAULT_TRACE_MAX; // unlimited traces
    memset(&lexgion_default_trace_config->end_action, 0,
           sizeof(lexgion_default_trace_config->end_action));

  // Mark the default lexgion trace config for each domain as empty config
  // codeptr: NULL: empty config
  //          i+1: lexgion_default + domain_default
  //          i+2: overwritten by user specified lexgion domain default in the
  //          config file
  for (int i = 0; i < num_domain; i++) {
    lexgion_domain_default_trace_config[i].codeptr =
        NULL; // NULL for empty config
  }

  pinsight_load_trace_config(NULL);
  setup_trace_config_env();

  /* Never-fires guard: the 'all' count policy fires only once every region this
   * thread has seen has capped; if a region is rare/never re-reached the TRACING
   * window may never end. Recommend a window_timeout backstop. 'first' always
   * fires, so it needs no warning. (Warn only; do not error or inject a default.) */
  {
    window_end_action_t *ma = &lexgion_default_trace_config->end_action;
    if (ma->trigger == TRIGGER_ALL && ma->window_timeout_sec <= 0) {
      fprintf(stderr, "PInsight WARNING: window_end_trigger='all' with no "
                      "window_timeout — the tracing window may never end if a "
                      "region is rare or never re-reached. Consider setting "
                      "window_timeout.\n");
    }
  }

#ifdef PINSIGHT_PYTHON
  /* After config file and env vars have been applied, save the intended Python
   * domain mode into last_mode, then force the domain back to STANDBY.
   * The launcher calls _pinsight_python.set_trace_mode() after all callbacks
   * are registered (imports complete) to restore the intended mode.
   * This prevents stdlib imports from consuming per-thread lexgion cache slots. */
  domain_default_trace_config[Python_domain_index].last_mode =
      domain_default_trace_config[Python_domain_index].mode;
  domain_default_trace_config[Python_domain_index].mode =
      PINSIGHT_DOMAIN_STANDBY;
#endif

  /* Signal handler is now installed by pinsight_control_thread_start()
   * in enter_exit.c — no need to call pinsight_install_signal_handler() here. */

#ifdef PINSIGHT_PRINT_CONFIG
  // Print domain info
  for (int di = 0; di < num_domain; di++) {
    struct domain_info *d = &domain_info_table[di];
    dsl_print_domain_info(d);
  }
  print_domain_trace_config(stdout);
  print_lexgion_trace_config(stdout);
  pinsight_print_knob_config(stdout);
#endif
}

#ifdef PINSIGHT_CUDA
static inline int pinsight_cuda_runtime_available(void) {
  static int cached = -1;
  if (cached != -1) {
    return cached;
  }
  void *handle = dlopen("libcuda.so.1", RTLD_LAZY | RTLD_LOCAL);
  if (!handle) {
    handle = dlopen("libcuda.so", RTLD_LAZY | RTLD_LOCAL);
  }
  if (!handle) {
    fprintf(
        stderr,
        "[PInsight WARNING] CUDA support was compiled in, but libcuda.so is "
        "not available on this system. CUDA tracing will be disabled.\n");
    cached = 0;
    return 0;
  }
  dlclose(handle);
  cached = 1;
  return 1;
}
#endif

#ifdef PINSIGHT_HIP
static inline int pinsight_hip_runtime_available(void) {
  static int cached = -1;
  if (cached != -1) {
    return cached;
  }
  void *handle = dlopen("libroctracer64.so", RTLD_LAZY | RTLD_LOCAL);
  if (!handle) {
    handle = dlopen("libroctracer64.so.4", RTLD_LAZY | RTLD_LOCAL);
  }
  if (!handle) {
    fprintf(
        stderr,
        "[PInsight WARNING] HIP support was compiled in, but libroctracer64.so "
        "is not available on this system. HIP tracing will be disabled.\n");
    cached = 0;
    return 0;
  }
  dlclose(handle);
  cached = 1;
  return 1;
}
#endif

/*
 * Pretty-print a domain into a file:
 *      <domain>_trace_config.install
 */
void dsl_print_domain_info(struct domain_info *d) {
  if (!d)
    return;

  char filename[256];
  snprintf(filename, sizeof(filename), "%s_trace_config.install", d->name);

  FILE *fp = fopen(filename, "w");
  if (!fp) {
    fprintf(stderr, "dsl_print_domain_info: cannot open file %s\n", filename);
    return;
  }

  /* [Domain.default] section: trace_mode and punit ranges */
  const char *mode_str = pinsight_mode_str(d->starting_mode);
  fprintf(fp, "[%s.default]\n", d->name);
  fprintf(fp, "    trace_mode = %s\n", mode_str);
  for (int i = 0; i < d->num_punits; ++i) {
    struct punit *p = &d->punits[i];
    fprintf(fp, "    %s.%s = (%u, %u)\n", d->name, p->name, p->low, p->high);
  }
  fprintf(fp, "\n");

  /* [Domain(subdomain).default] sections: events with on/off status */
  for (int s = 0; s < d->num_subdomains; ++s) {
    struct subdomain *sub = &d->subdomains[s];

    fprintf(fp, "[%s(%s).default]\n", d->name, sub->name);

    for (int eid = 0; eid < d->event_id_upper; ++eid) {
      struct event *ev = &d->event_table[eid];
      if (!ev->valid)
        continue;
      if (ev->subdomain != s)
        continue;

      int enabled = (d->eventInstallStatus >> eid) & 1;
      fprintf(fp, "    %s = %s\n", ev->name, enabled ? "on" : "off");
    }

    fprintf(fp, "\n");
  }

  fclose(fp);
}

// Helper to print domain punit set
// filter_domain_idx: -1 for all.
// exclude_mode: 0 = include only filter_domain_idx, 1 = exclude
// filter_domain_idx
static void print_punit_set_filtered(FILE *out, domain_punit_set_t *set_array,
                                     int *first_printed, int filter_domain_idx,
                                     int exclude_mode) {
  for (int di = 0; di < num_domain; di++) {
    // Apply Filter
    if (filter_domain_idx >= 0) {
      if (exclude_mode && di == filter_domain_idx)
        continue;
      if (!exclude_mode && di != filter_domain_idx)
        continue;
    }

    if (set_array[di].set) {
      struct domain_info *target_domain = &domain_info_table[di];
      int has_punits = 0;
      for (int pi = 0; pi < target_domain->num_punits; pi++) {
        if (set_array[di].punit[pi].set) {
          has_punits = 1;
          if (*first_printed == 0) {
            *first_printed = 1;
          } else {
            fprintf(out, ", ");
          }

          BitSet *bs = &set_array[di].punit[pi].punit_ids;
          char *range_str = bitset_to_rangestring(bs);
          if (range_str) {
            fprintf(out, "%s.%s(%s)", target_domain->name,
                    target_domain->punits[pi].name, range_str);
            free(range_str);
          } else {
            fprintf(out, "%s.%s()", target_domain->name,
                    target_domain->punits[pi].name);
          }
        }
      }
    }
  }
}

void print_domain_trace_config(FILE *out) {
  if (!out)
    return;

  for (int i = 0; i < num_domain; i++) {
    struct domain_info *d = &domain_info_table[i];
    const char *mode_str = pinsight_mode_str(domain_default_trace_config[i].mode);
    fprintf(out, "[%s.default]\n", d->name);
    fprintf(out, "    trace_mode = %s\n", mode_str);
    for (int p = 0; p < d->num_punits; p++) {
      struct punit *pu = &d->punits[p];
      fprintf(out, "    %s.%s = (%u, %u)\n", d->name, pu->name, pu->low,
              pu->high);
    }
    unsigned long current_events = domain_default_trace_config[i].events;
    for (int k = 0; k < d->num_events; k++) {
      if (strlen(d->event_table[k].name) == 0)
        continue;
      if (k >= 64)
        break;
      int on = (current_events >> k) & 1;
      fprintf(out, "    %s = %s\n", d->event_table[k].name, on ? "on" : "off");
    }
    fprintf(out, "\n");

    punit_trace_config_t *curr = domain_punit_trace_config[i];
    while (curr) {
      // Part 1: [Target]
      fprintf(out, "[");
      int first = 0;
      // Print ONLY the target domain punits
      print_punit_set_filtered(out, curr->domain_punits, &first, i, 0);
      fprintf(out, "]");

      // Part 2: : Inheritance (Always matches domain default for Domain config)
      fprintf(out, ": %s.default", d->name);

      // Part 3: : PunitSet (Other domains)
      first = 0;
      // Check if there are other domains to print
      // We can buffer it or just check if anything WOULD be printed, but
      // `print_punit_set` modifies stream. Let's use a temp buffer or just
      // print a separator if lexgion_set_top_trace_bitneeded? Simpler: Print
      // comma/colon logic inside? No. Let's check if others exist.
      int others_exist = 0;
      for (int k = 0; k < num_domain; k++) {
        if (k == i)
          continue;
        if (curr->domain_punits[k].set) {
          // Check if it has punits
          for (int p = 0; p < domain_info_table[k].num_punits; p++)
            if (curr->domain_punits[k].punit[p].set)
              others_exist = 1;
        }
      }

      if (others_exist) {
        fprintf(out, " : ");
        first = 0; // Reset for this section
        print_punit_set_filtered(out, curr->domain_punits, &first, i, 1);
      }

      fprintf(out, "\n");

      unsigned long p_events = curr->events;
      for (int k = 0; k < d->num_events; k++) {
        if (strlen(d->event_table[k].name) == 0)
          continue;
        if (k >= 64)
          break;
        int on = (p_events >> k) & 1;
        fprintf(out, "    %s = %s\n", d->event_table[k].name,
                on ? "on" : "off");
      }
      fprintf(out, "\n");
      curr = curr->next;
    }
  }
}

// Helper to print a single lexgion config entry with the given header
static void print_single_lexgion_config(FILE *out, lexgion_trace_config_t *lg,
                                        const char *header) {
  fprintf(out, "%s\n", header);

  fprintf(out, "    trace_starts_at = %d\n", lg->trace_starts_at);
  fprintf(out, "    max_num_traces = %d\n", lg->max_num_traces);
  fprintf(out, "    tracing_rate = %d\n", lg->tracing_rate);
  {
    window_end_action_t *ma = &lg->end_action;
    if (ma->introspect) {
      fprintf(out, "    window_end_action = INTROSPECT:%d:%s",
              ma->introspect_pause_duration,
              ma->introspect_script[0] ? ma->introspect_script : "-");
      // Print resume mode (use first domain's mode as representative)
      int has_resume = 0;
      for (int d = 0; d < num_domain; d++) {
        if (ma->mode[d] != PINSIGHT_DOMAIN_NONE) {
          has_resume = 1;
          break;
        }
      }
      if (has_resume) {
        // Check if all domains have the same resume mode
        pinsight_domain_mode_t common = ma->mode[0];
        int all_same = 1;
        for (int d = 1; d < num_domain; d++) {
          if (ma->mode[d] != common) {
            all_same = 0;
            break;
          }
        }
        if (all_same && common != PINSIGHT_DOMAIN_NONE) {
          const char *mode_str = pinsight_mode_str(common);
          fprintf(out, ":%s", mode_str);
        }
      }
      fprintf(out, "\n");
    } else {
      int has_end_action = 0;
      for (int d = 0; d < num_domain; d++) {
        if (ma->mode[d] != PINSIGHT_DOMAIN_NONE) {
          has_end_action = 1;
          break;
        }
      }
      if (has_end_action) {
        fprintf(out, "    window_end_action =");
        int first = 1;
        for (int d = 0; d < num_domain; d++) {
          if (ma->mode[d] != PINSIGHT_DOMAIN_NONE) {
            const char *mode_str = pinsight_mode_str(ma->mode[d]);
            if (!first)
              fprintf(out, ",");
            fprintf(out, " %s:%s", domain_info_table[d].name, mode_str);
            first = 0;
          }
        }
        fprintf(out, "\n");
      }
    }
  }

  for (int di = 0; di < num_domain; di++) {
    if (lg->domain_events[di].set) {
      struct domain_info *d = &domain_info_table[di];
      unsigned long evt = lg->domain_events[di].events;
      for (int k = 0; k < d->num_events; k++) {
        if (strlen(d->event_table[k].name) == 0)
          continue;
        if (k >= 64)
          break;
        int on = (evt >> k) & 1;
        fprintf(out, "    %s.%s = %s\n", d->name, d->event_table[k].name,
                on ? "on" : "off");
      }
    }
  }
  fprintf(out, "\n");
}

void print_lexgion_trace_config(FILE *out) {
  if (!out)
    return;

  // 1. Print global Lexgion.default
  print_single_lexgion_config(out, lexgion_default_trace_config,
                              "[Lexgion.default]");

  // 2. Print domain-specific Lexgion(Domain).default for each configured domain
  for (int di = 0; di < num_domain; di++) {
    lexgion_trace_config_t *dlg = &lexgion_domain_default_trace_config[di];
    if (dlg->codeptr != NULL) { // Non-NULL marker means it was configured
      char header[128];
      snprintf(header, sizeof(header), "[Lexgion(%s).default]",
               domain_info_table[di].name);
      print_single_lexgion_config(out, dlg, header);
    }
  }

  // 3. Print address-specific lexgion configs
  for (int i = 0; i < num_lexgion_trace_configs; i++) {
    lexgion_trace_config_t *lg = &lexgion_trace_config[i];

    // Build header with inheritance and punit set
    fprintf(out, "[Lexgion(%p)]", lg->codeptr);

    // Part 2: Inheritance
    int first_inh = 1;
    int printed_inh = 0;

    for (int di = 0; di < num_domain; di++) {
      if (lg->domain_punits[di].set) {
        int has_punits = 0;
        struct domain_info *d = &domain_info_table[di];
        for (int p = 0; p < d->num_punits; p++) {
          if (lg->domain_punits[di].punit[p].set)
            has_punits = 1;
        }

        if (!has_punits) {
          if (first_inh) {
            fprintf(out, ": ");
            first_inh = 0;
          } else {
            fprintf(out, ", ");
          }
          fprintf(out, "%s.default", d->name);
          printed_inh = 1;
        }
      }
    }

    // Part 3: Punit Set
    int has_any_punits = 0;
    for (int di = 0; di < num_domain; di++) {
      if (lg->domain_punits[di].set) {
        struct domain_info *d = &domain_info_table[di];
        for (int p = 0; p < d->num_punits; p++) {
          if (lg->domain_punits[di].punit[p].set)
            has_any_punits = 1;
        }
      }
    }

    if (has_any_punits) {
      if (!printed_inh) {
        fprintf(out, ": ");
      }
      fprintf(out, " : ");
      int printed_p = 0;
      for (int di = 0; di < num_domain; di++) {
        if (lg->domain_punits[di].set) {
          struct domain_info *d = &domain_info_table[di];
          for (int pi = 0; pi < d->num_punits; pi++) {
            if (lg->domain_punits[di].punit[pi].set) {
              if (printed_p)
                fprintf(out, ", ");
              printed_p = 1;

              BitSet *bs = &lg->domain_punits[di].punit[pi].punit_ids;
              char *range = bitset_to_rangestring(bs);
              if (range) {
                fprintf(out, "%s.%s(%s)", d->name, d->punits[pi].name, range);
                free(range);
              } else {
                fprintf(out, "%s.%s()", d->name, d->punits[pi].name);
              }
            }
          }
        }
      }
    }

    fprintf(out, "\n");

    fprintf(out, "    trace_starts_at = %d\n", lg->trace_starts_at);
    fprintf(out, "    max_num_traces = %d\n", lg->max_num_traces);
    fprintf(out, "    tracing_rate = %d\n", lg->tracing_rate);

    for (int di = 0; di < num_domain; di++) {
      if (lg->domain_events[di].set) {
        struct domain_info *d = &domain_info_table[di];
        unsigned long evt = lg->domain_events[di].events;
        for (int k = 0; k < d->num_events; k++) {
          if (strlen(d->event_table[k].name) == 0)
            continue;
          if (k >= 64)
            break;
          int on = (evt >> k) & 1;
          fprintf(out, "    %s.%s = %s\n", d->name, d->event_table[k].name,
                  on ? "on" : "off");
        }
      }
    }
    fprintf(out, "\n");
  }
}

long env_get_long(const char *varname, long default_value) {
  const char *str = getenv(varname);
  long out = default_value;
  // strtol segfaults if given a NULL ptr. Check before use!
  if (str != NULL) {
    out = strtol(str, NULL, 0);
  }
  // Error occurred in parsing, return default value.
  if (errno == EINVAL || errno == ERANGE) {
    out = default_value;
  }
  return out;
}

unsigned long env_get_ulong(const char *varname, unsigned long default_value) {
  const char *str = getenv(varname);
  unsigned long out = default_value;
  // strtoul segfaults if given a NULL ptr. Check before use!
  if (str != NULL) {
    out = strtoul(str, NULL, 0);
  }
  // Error occurred in parsing, return default value.
  if (errno == EINVAL || errno == ERANGE) {
    out = default_value;
  }
  return out;
}

// ============================================================
// Node-policy resolution (device_activity / energy measure).
// Resolved once at startup (per-run). WHO measures; the domain mode / windowing
// is orthogonal. See doc/node_singleton_measurement_design.md.
// ============================================================

// Local rank within node, published by the MPI wrapper (§6.5); -1 if unknown.
int pinsight_mpi_local_rank = -1;
// Ranks per node, published by the MPI wrapper (§6.5); -1 if unknown.
int pinsight_mpi_ranks_per_node = -1;

// Energy node-singleton policy (default ON = every rank; today's behavior).
// Defined here so the standalone config-parser test links without energy.c.
pinsight_nodepolicy_t energy_measure_policy = PINSIGHT_NODEPOLICY_ON;

const char *pinsight_nodepolicy_str(pinsight_nodepolicy_t p) {
  switch (p) {
  case PINSIGHT_NODEPOLICY_OFF:            return "off";
  case PINSIGHT_NODEPOLICY_ON:             return "on";
  case PINSIGHT_NODEPOLICY_ANYONE_PER_NODE:return "anyone_per_node";
  case PINSIGHT_NODEPOLICY_LEADER_PER_NODE:return "leader_per_node";
  case PINSIGHT_NODEPOLICY_ROTATE_PER_NODE:return "rotate_per_node";
  default:                                 return "off";
  }
}

pinsight_nodepolicy_val_t
pinsight_parse_nodepolicy(const char *val, pinsight_nodepolicy_t dflt) {
  pinsight_nodepolicy_val_t r = {dflt, 0};
  if (!val) return r;
  while (*val == ' ' || *val == '\t') val++;
  const char *colon = strchr(val, ':');
  size_t n = colon ? (size_t)(colon - val) : strlen(val);
  char base[64];
  if (n >= sizeof(base)) n = sizeof(base) - 1;
  memcpy(base, val, n);
  base[n] = '\0';
  while (n > 0 && (base[n - 1] == ' ' || base[n - 1] == '\t' ||
                   base[n - 1] == '\r' || base[n - 1] == '\n'))
    base[--n] = '\0';

  if (strcasecmp(base, "off") == 0)
    r.policy = PINSIGHT_NODEPOLICY_OFF;
  else if (strcasecmp(base, "on") == 0)
    r.policy = PINSIGHT_NODEPOLICY_ON;
  else if (strcasecmp(base, "anyone_per_node") == 0)
    r.policy = PINSIGHT_NODEPOLICY_ANYONE_PER_NODE;
  else if (strcasecmp(base, "leader_per_node") == 0)
    r.policy = PINSIGHT_NODEPOLICY_LEADER_PER_NODE;
  else if (strcasecmp(base, "rotate_per_node") == 0) {
    r.policy = PINSIGHT_NODEPOLICY_ROTATE_PER_NODE;
    r.param = colon ? atoi(colon + 1) : 0;
    if (r.param <= 0) r.param = 1000; /* default period ms */
  } else {
    fprintf(stderr, "PInsight: unknown nodepolicy value '%s' — using '%s'\n",
            base, pinsight_nodepolicy_str(dflt));
    r.policy = dflt;
  }
  return r;
}

int pinsight_get_nodepolicy_index(int domain_index, const char *key) {
  if (domain_index < 0 || domain_index >= num_domain) return -1;
  struct domain_info *d = &domain_info_table[domain_index];
  for (int i = 0; i < d->num_nodepolicy_keys; i++)
    if (strcmp(d->nodepolicy_keys[i].name, key) == 0) return i;
  return -1;
}

typedef enum { NODE_UNKNOWN = -1, NODE_NONLEADER = 0, NODE_LEADER = 1 }
    node_leader_status_t;

static int local_rank_from_env(void) {
  static const char *vars[] = {"FLUX_TASK_LOCAL_ID", "SLURM_LOCALID",
                               "OMPI_COMM_WORLD_LOCAL_RANK", "MPI_LOCALRANKID",
                               NULL};
  for (int i = 0; vars[i]; i++) {
    const char *v = getenv(vars[i]);
    if (v && *v) return atoi(v);
  }
  return -1;
}

/* Public accessors (Phase 2 rotate needs these; leader election uses the same
 * sources internally). Env is authoritative-at-constructor; MPI-published
 * values fill in post-MPI_Init when no env var exists. */
int pinsight_local_rank(void) {
  int lr = local_rank_from_env();
  if (lr >= 0) return lr;
  return pinsight_mpi_local_rank; /* -1 if unknown */
}

int pinsight_ranks_per_node(void) {
  if (pinsight_mpi_ranks_per_node > 0) return pinsight_mpi_ranks_per_node;
  const char *v = getenv("SLURM_NTASKS_PER_NODE");
  if (v && *v) {
    int n = atoi(v); /* handles "4" and the "4(x16)" form (atoi stops at '(') */
    if (n > 0) return n;
  }
  return -1;
}

/* §4.3 chain: launcher env -> local-rank env -> MPI local rank -> UNKNOWN. */
static node_leader_status_t node_leader_status(void) {
  const char *v = getenv("PINSIGHT_NODE_LEADER");
  if (v && *v) return (atoi(v) == 1) ? NODE_LEADER : NODE_NONLEADER;
  int lr = local_rank_from_env();
  if (lr >= 0) return (lr == 0) ? NODE_LEADER : NODE_NONLEADER;
  if (pinsight_mpi_local_rank >= 0)
    return (pinsight_mpi_local_rank == 0) ? NODE_LEADER : NODE_NONLEADER;
  return NODE_UNKNOWN;
}

/* flock a node-local file; exactly one process/node wins & holds it. Cached. */
static int claim_node_singleton(const char *name) {
  static struct { char name[32]; int won; } cache[8];
  static int ncache = 0;
  for (int i = 0; i < ncache; i++)
    if (strcmp(cache[i].name, name) == 0) return cache[i].won;

  const char *user = getenv("USER");
  if (!user || !*user) user = "pinsight";
  char dir[256], path[400];
  snprintf(dir, sizeof(dir), "/tmp/%s", user);
  mkdir(dir, 0700); /* best-effort */
  snprintf(path, sizeof(path), "%s/pinsight_%s_singleton.lock", dir, name);

  int won = 0;
  int fd = open(path, O_CREAT | O_RDWR, 0600);
  if (fd >= 0) {
    won = (flock(fd, LOCK_EX | LOCK_NB) == 0) ? 1 : 0;
    if (!won) close(fd); /* winner keeps fd for process lifetime */
  }
  if (ncache < (int)(sizeof(cache) / sizeof(cache[0]))) {
    strncpy(cache[ncache].name, name, sizeof(cache[ncache].name) - 1);
    cache[ncache].won = won;
    ncache++;
  }
  return won;
}

int pinsight_node_role(const char *lockname, pinsight_nodepolicy_t policy) {
  static struct { char name[32]; int role; } cache[8];
  static int ncache = 0;
  for (int i = 0; i < ncache; i++)
    if (strcmp(cache[i].name, lockname) == 0) return cache[i].role;

  int role;
  switch (policy) {
  case PINSIGHT_NODEPOLICY_OFF:
    role = 0;
    break;
  case PINSIGHT_NODEPOLICY_ON:
    role = 1;
    break;
  case PINSIGHT_NODEPOLICY_ANYONE_PER_NODE:
    role = claim_node_singleton(lockname);
    break;
  case PINSIGHT_NODEPOLICY_LEADER_PER_NODE:
  case PINSIGHT_NODEPOLICY_ROTATE_PER_NODE: { /* Phase 1: rotate == leader */
    node_leader_status_t s = node_leader_status();
    role = (s == NODE_LEADER)      ? 1
           : (s == NODE_NONLEADER) ? 0
                                   : claim_node_singleton(lockname);
    break;
  }
  default:
    role = 0;
    break;
  }
  if (ncache < (int)(sizeof(cache) / sizeof(cache[0]))) {
    strncpy(cache[ncache].name, lockname, sizeof(cache[ncache].name) - 1);
    cache[ncache].role = role;
    ncache++;
  }
  return role;
}
