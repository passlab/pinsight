#include <ctype.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app_knob.h"
#include "bitset.h"
#include "trace_config.h"

// Forward declarations
static int process_line(char *line);
static int parse_section_header(char *line);
static void parse_key_value(char *line);
static int parse_domain_punit_spec(char *spec, int *domain_idx,
                                   int *punit_kind_idx, unsigned int *low,
                                   unsigned int *high);
static int parse_range_string(const char *range_str, unsigned int *low,
                              unsigned int *high, BitSet *range_mask);
static lexgion_trace_config_t *get_or_create_lexgion_config(void *codeptr);
static punit_trace_config_t *
get_or_create_punit_config(int domain_idx, BitSet *punit_mask, int punit_kind,
                           unsigned int low, unsigned int high);

// State variables for parsing context of the four kinds of sections
typedef enum {
  SECTION_NONE,
  SECTION_DOMAIN_DEFAULT,
  SECTION_DOMAIN_GLOBAL,
  SECTION_DOMAIN_PUNIT,
  SECTION_LEXGION_ADDRESS,
  SECTION_LEXGION_DEFAULT,
  SECTION_LEXGION_DOMAIN_DEFAULT,
  SECTION_KNOB
} SectionType;

typedef enum {
  ACTION_SET,   // Default: merge new settings with existing (or create new)
  ACTION_RESET, // Revert to computed/system defaults (*.default sections only,
                // no body)
  ACTION_REMOVE // Delete/disable config (Domain.punit and Lexgion(address)
                // only, no body)
} ConfigAction;

static SectionType current_section_type = SECTION_NONE;
static int current_domain_idx = -1;
static punit_trace_config_t *current_punit_config = NULL;
static lexgion_trace_config_t *current_lexgion_config = NULL;

// Multi-address Lexgion support: Lexgion(addr1, addr2, ...)
#define MAX_MULTI_LEXGION 64
static lexgion_trace_config_t *current_lexgion_configs[MAX_MULTI_LEXGION];
static int num_current_lexgion_configs = 0;

#define MAX_LINE_LENGTH 1024

// --- Utility Functions ---

static pinsight_domain_mode_t parse_mode_value(const char *val) {
  if (strcasecmp(val, "OFF") == 0)
    return PINSIGHT_DOMAIN_OFF;
  if (strcasecmp(val, "MONITORING") == 0 || strcasecmp(val, "MONITOR") == 0)
    return PINSIGHT_DOMAIN_MONITORING;
  return PINSIGHT_DOMAIN_TRACING;
}

/**
 * Unified parser for trace_mode_after values.
 * Handles:
 *   "MONITORING"                     -> mode[*]=MONITORING, pause=0
 *   "OpenMP:OFF, MPI:MONITORING"     -> per-domain modes, pause=0
 *   "PAUSE:60:script.sh"             -> pause=1, resume all MONITORING
 *   "PAUSE:60:script.sh:TRACING"     -> pause=1, resume all TRACING
 *   "PAUSE:0:-"                      -> pause indefinitely, no script
 * Returns 0 on success, -1 on error.
 */
int parse_trace_mode_after(const char *val, trace_mode_after_t *out) {
  memset(out, 0, sizeof(*out));

  if (strncasecmp(val, "PAUSE:", 6) == 0) {
    // PAUSE:timeout:script[:resume_mode]
    out->pause = 1;
    const char *p = val + 6;

    // Parse timeout
    char *endptr;
    long timeout = strtol(p, &endptr, 10);
    out->pause_timeout = (timeout > 0) ? (int)timeout : 0;

    // Expect ':' after timeout
    if (*endptr != ':') {
      fprintf(stderr, "PInsight config: invalid PAUSE syntax '%s', "
                      "expected PAUSE:timeout:script[:mode]\n", val);
      return -1;
    }
    p = endptr + 1;

    // Parse script (up to next ':' or end of string)
    const char *colon = strchr(p, ':');
    if (colon) {
      size_t len = colon - p;
      if (len >= sizeof(out->pause_script))
        len = sizeof(out->pause_script) - 1;
      strncpy(out->pause_script, p, len);
      out->pause_script[len] = '\0';

      // Parse optional resume_mode
      pinsight_domain_mode_t resume = parse_mode_value(colon + 1);
      for (int i = 0; i < MAX_NUM_DOMAINS; i++)
        out->mode[i] = resume;
    } else {
      strncpy(out->pause_script, p, sizeof(out->pause_script) - 1);
      out->pause_script[sizeof(out->pause_script) - 1] = '\0';

      // Default resume mode: MONITORING for all domains
      for (int i = 0; i < MAX_NUM_DOMAINS; i++)
        out->mode[i] = PINSIGHT_DOMAIN_MONITORING;
    }
    return 0;
  }

  // Non-PAUSE: existing comma-separated mode parsing
  // "MONITORING" or "OpenMP:OFF, MPI:MONITORING"
  char val_copy[MAX_LINE_LENGTH];
  strncpy(val_copy, val, sizeof(val_copy) - 1);
  val_copy[sizeof(val_copy) - 1] = '\0';

  char *saveptr;
  char *token = strtok_r(val_copy, ",", &saveptr);
  while (token) {
    char *trimmed = token;
    while (*trimmed == ' ') trimmed++;

    char *colon = strchr(trimmed, ':');
    if (colon) {
      // Explicit: "OpenMP:MONITORING"
      *colon = '\0';
      char *domain_name = trimmed;
      // Trim trailing spaces from domain name
      char *end = domain_name + strlen(domain_name) - 1;
      while (end > domain_name && *end == ' ') *end-- = '\0';

      char *mode_str = colon + 1;
      while (*mode_str == ' ') mode_str++;

      int d_idx = find_domain_index(domain_name);
      if (d_idx >= 0) {
        out->mode[d_idx] = parse_mode_value(mode_str);
      } else {
        fprintf(stderr, "PInsight config: unknown domain '%s' in "
                        "trace_mode_after\n", domain_name);
      }
    } else {
      // Shorthand: "MONITORING" -> apply to all registered domains
      // Trim trailing spaces
      char *end = trimmed + strlen(trimmed) - 1;
      while (end > trimmed && *end == ' ') *end-- = '\0';

      pinsight_domain_mode_t m = parse_mode_value(trimmed);
      for (int i = 0; i < num_domain; i++)
        out->mode[i] = m;
    }
    token = strtok_r(NULL, ",", &saveptr);
  }
  return 0;
}

static char *trim_whitespace(char *str) {
  char *end;
  while (isspace((unsigned char)*str))
    str++;
  if (*str == 0)
    return str;
  end = str + strlen(str) - 1;
  while (end > str && isspace((unsigned char)*end))
    end--;
  *(end + 1) = 0;
  return str;
}

// Parses "4, 6, 8-12" into a BitSet. Only relevant if we were using bitsets for
// single punit check, but the current struct punit_trace_config uses a BitSet
// for punit_ids. Returns 0 on success.
static int parse_range_list(const char *str, BitSet *mask) {
  return bitset_parse_ranges(mask, str);
}

// --- Parsing Logic ---

void parse_trace_config_file(char *filename) {
  FILE *fp = fopen(filename, "r");
  if (!fp) {
    fprintf(stderr, "PInSight: Could not open trace configuration file %s.\n",
            filename);
    return;
  }

  // Reset parser state to avoid leaking state from previous calls
  current_section_type = SECTION_NONE;
  current_domain_idx = -1;
  current_lexgion_config = NULL;
  current_punit_config = NULL;
  num_current_lexgion_configs = 0;

  char line[MAX_LINE_LENGTH];
  int line_num = 0;
  while (fgets(line, sizeof(line), fp)) {
    line_num++;
    if (process_line(line) != 0) {
      fprintf(stderr, "PInSight: Error parsing config file %s at line %d: %s",
              filename, line_num, line);
      break;
    }
  }

  fclose(fp);
}

static int process_line(char *line) {
  char *trimmed = trim_whitespace(line);
  if (strlen(trimmed) == 0 || trimmed[0] == '#')
    return 0;

  if (trimmed[0] == '[') {
    return parse_section_header(trimmed);
  } else {
    parse_key_value(trimmed);
    return 0;
  }
}

// --- Helper Structs for Punit Parsing ---
// --- Forward Declarations ---
int find_domain_index(const char *name);
static int find_punit_kind_index(int domain_idx, const char *punit_name);
static int apply_inheritance(lexgion_trace_config_t *lg_config,
                             char *inheritance_str);
static int parse_punit_set_string(char *spec_str,
                                  domain_punit_set_t *set_array);
static void reset_domain_default_config(int domain_idx);
static void reset_lexgion_config(lexgion_trace_config_t *lg);
static punit_trace_config_t *
find_exact_punit_config(int domain_idx, domain_punit_set_t *target_set);

// Example section: [ACTION Target] : Inheritance : PunitSet
// Target: Lexgion(...) or Domain.PunitKind(Set)
static int parse_section_header(char *line) {
  // 1. Locate closing bracket ']'
  char *close_bracket = strchr(line, ']');
  if (!close_bracket)
    return -1;

  *close_bracket = '\0';
  char *inside_brackets = line + 1;
  char *outside_brackets = close_bracket + 1;

  // 2. Parse Action and Target from inside brackets
  ConfigAction action = ACTION_SET; // Default
  char *target = inside_brackets;

  // Check for prefixes
  if (strncasecmp(inside_brackets, "SET ", 4) == 0) {
    action = ACTION_SET;
    target = inside_brackets + 4;
  } else if (strncasecmp(inside_brackets, "RESET ", 6) == 0) {
    action = ACTION_RESET;
    target = inside_brackets + 6;
  } else if (strncasecmp(inside_brackets, "REMOVE ", 7) == 0) {
    action = ACTION_REMOVE;
    target = inside_brackets + 7;
  }
  target = trim_whitespace(target);

  // 3. Parse Inheritance and PunitSet from outside brackets
  char *parts[2] = {NULL, NULL}; // [0]=Inheritance, [1]=PunitSet
  int part_count = 0;

  // Check for leading colon
  char *ptr = outside_brackets;
  while (isspace((unsigned char)*ptr))
    ptr++;
  if (*ptr == ':') {
    ptr++; // Skip colon
    char *token = strtok(ptr, ":");
    while (token && part_count < 2) {
      parts[part_count++] = trim_whitespace(token);
      token = strtok(NULL, ":");
    }
  }

  // --- Process Target ---
  current_section_type = SECTION_NONE;
  current_domain_idx = -1;
  current_lexgion_config = NULL;
  current_punit_config = NULL;

  // --- Determine if this is a default-kind section (for action validation) ---
  int is_default_section = 0;

  // Case 1: Lexgion.default, Lexgion(Domain).default, or Lexgion(0x...)
  if (strncmp(target, "Lexgion", 7) == 0) {
    lexgion_trace_config_t *lg = NULL;
    if (strcmp(target, "Lexgion.default") == 0) {
      current_section_type = SECTION_LEXGION_DEFAULT;
      is_default_section = 1;
      lg = lexgion_default_trace_config;
    } else if (strncmp(target, "Lexgion(", 8) == 0) {
      // Check for Lexgion(Domain).default pattern, e.g. Lexgion(OpenMP).default
      char *ptr_start = strchr(target, '(');
      char *ptr_end = strchr(target, ')');
      if (ptr_start && ptr_end) {
        // Check if ".default" follows the closing paren
        if (strcmp(ptr_end + 1, ".default") == 0) {
          // Extract domain name
          *ptr_end = '\0';
          char *domain_name = ptr_start + 1;
          int d_idx = find_domain_index(domain_name);
          if (d_idx >= 0) {
            current_section_type = SECTION_LEXGION_DOMAIN_DEFAULT;
            current_domain_idx = d_idx;
            is_default_section = 1;
            lg = &lexgion_domain_default_trace_config[d_idx];
            // Initialize from Lexgion.default + Domain.default so user
            // key-values apply on top of the correct base.  Also marks
            // codeptr non-NULL so the fill loop in
            // pinsight_load_trace_config() skips this entry.
            *lg = *lexgion_default_trace_config;
            lg->codeptr = (void *)(uintptr_t)(d_idx + 2);
            lg->domain_events[d_idx].set = 1;
            lg->domain_events[d_idx].events =
                domain_default_trace_config[d_idx].events;
          }
        } else {
          // Lexgion(0x...) or Lexgion(0x..., 0x..., ...) - address-specific
          current_section_type = SECTION_LEXGION_ADDRESS;
          *ptr_end = '\0';
          char *addr_list = ptr_start + 1;

          // Parse comma-separated addresses
          num_current_lexgion_configs = 0;
          char *saveptr = NULL;
          char *token = strtok_r(addr_list, ",", &saveptr);
          while (token && num_current_lexgion_configs < MAX_MULTI_LEXGION) {
            char *trimmed = trim_whitespace(token);
            if (*trimmed) {
              uint64_t addr = strtoull(trimmed, NULL, 0);
              void *codeptr = (void *)(uintptr_t)addr;
              lexgion_trace_config_t *lc =
                  get_or_create_lexgion_config(codeptr);
              lc->removed = 0;
              current_lexgion_configs[num_current_lexgion_configs++] = lc;
            }
            token = strtok_r(NULL, ",", &saveptr);
          }
          // Set lg to first config for default-section validation path
          if (num_current_lexgion_configs > 0) {
            lg = current_lexgion_configs[0];
          }
        }
      }
    }

    if (lg) {
      current_lexgion_config = lg;

      // Validate action-target combinations
      if (action == ACTION_REMOVE && is_default_section) {
        fprintf(stderr,
                "PInsight Warning: REMOVE is not valid for default sections. "
                "Use RESET instead. Ignoring [REMOVE %s].\n",
                target);
        current_section_type = SECTION_NONE;
        return 0;
      }
      if (action == ACTION_RESET && !is_default_section) {
        fprintf(stderr,
                "PInsight Warning: RESET is only valid for *.default sections. "
                "Use REMOVE instead. Ignoring [RESET %s].\n",
                target);
        current_section_type = SECTION_NONE;
        return 0;
      }

      // RESET: revert to computed defaults (no body expected)
      if (action == ACTION_RESET) {
        if (current_section_type == SECTION_LEXGION_DEFAULT) {
          // Reset entire object, then set non-zero system defaults
          memset(lg, 0, sizeof(*lg));
          lg->tracing_rate = DEFAULT_TRACE_RATE;
          lg->trace_starts_at = DEFAULT_TRACE_START;
          lg->max_num_traces = DEFAULT_TRACE_MAX;
        }
        current_section_type = SECTION_NONE; // No body for RESET
        return 0;
      }

      // REMOVE: only for Lexgion(address)
      if (action == ACTION_REMOVE) {
        for (int i = 0; i < num_current_lexgion_configs; i++) {
          current_lexgion_configs[i]->removed = 1;
        }
        current_section_type = SECTION_NONE; // Stop parsing body
        return 0;
      }

      // SET: Apply Inheritance (all Lexgion section types)
      if (parts[0]) {
        if (current_section_type == SECTION_LEXGION_ADDRESS) {
          for (int i = 0; i < num_current_lexgion_configs; i++) {
            apply_inheritance(current_lexgion_configs[i], parts[0]);
          }
        } else {
          // Lexgion.default or Lexgion(Domain).default
          apply_inheritance(lg, parts[0]);
        }
      }

      // SET: Apply PunitSet (only for Lexgion(address))
      if (parts[1] && current_section_type == SECTION_LEXGION_ADDRESS) {
        for (int i = 0; i < num_current_lexgion_configs; i++) {
          parse_punit_set_string(parts[1],
                                 current_lexgion_configs[i]->domain_punits);
        }
      }
    }
  }
  // Case 2a: Knob section
  else if (strcmp(target, "Knob") == 0) {
    current_section_type = SECTION_KNOB;
  }
  // Case 2b: Domain.global (must match before .default)
  else if (strstr(target, ".global")) {
    current_section_type = SECTION_DOMAIN_GLOBAL;
    is_default_section = 1; // RESET allowed, REMOVE not
    char *dot = strchr(target, '.');
    if (dot) {
      *dot = '\0';
      int idx = find_domain_index(target);
      if (idx >= 0) {
        current_domain_idx = idx;

        // Validate action
        if (action == ACTION_REMOVE) {
          fprintf(stderr, "PInsight Warning: REMOVE is not valid for "
                          "Domain.global. Use RESET instead. Ignoring.\n");
          current_section_type = SECTION_NONE;
          return 0;
        }

        // RESET: revert mode to install default (no body)
        if (action == ACTION_RESET) {
          domain_default_trace_config[idx].mode =
              domain_info_table[idx].starting_mode;
          domain_default_trace_config[idx].events =
              domain_info_table[idx].eventInstallStatus;
          current_section_type = SECTION_NONE;
          return 0;
        }

        // SET: proceed to parse body
      }
    }
  }
  // Case 2b: Domain.default
  else if (strstr(target, ".default")) {
    current_section_type = SECTION_DOMAIN_DEFAULT;
    is_default_section = 1;
    char *dot = strchr(target, '.');
    if (dot) {
      *dot = '\0';
      int idx = find_domain_index(target);
      if (idx >= 0) {
        current_domain_idx = idx;

        // Validate action
        if (action == ACTION_REMOVE) {
          fprintf(stderr, "PInsight Warning: REMOVE is not valid for "
                          "Domain.default. Use RESET instead. Ignoring.\n");
          current_section_type = SECTION_NONE;
          return 0;
        }

        // RESET: revert to system install defaults (no body)
        if (action == ACTION_RESET) {
          reset_domain_default_config(idx);
          current_section_type = SECTION_NONE; // No body for RESET
          return 0;
        }

        // SET: proceed to parse body (merge with existing)
      }
    }
  }
  // Case 3: Domain.punit specification
  else {
    current_section_type = SECTION_DOMAIN_PUNIT;

    // Validate action: RESET not valid for punit sections
    if (action == ACTION_RESET) {
      fprintf(stderr, "PInsight Warning: RESET is only valid for *.default "
                      "sections. Use REMOVE for punit sections. Ignoring.\n");
      current_section_type = SECTION_NONE;
      return 0;
    }

    // Check for wildcard REMOVE: [REMOVE Domain.punitKind(*)]
    char *open_paren = strchr(target, '(');
    char *close_paren = open_paren ? strchr(open_paren, ')') : NULL;
    if (action == ACTION_REMOVE && open_paren && close_paren) {
      // Check if the content between parens is "*"
      char *inner = open_paren + 1;
      while (isspace((unsigned char)*inner))
        inner++;
      if (*inner == '*') {
        // Wildcard REMOVE: Domain.punitKind(*)
        *open_paren = '\0'; // Truncate to "Domain.punitKind"
        char *dot = strchr(target, '.');
        int d_idx = -1;
        int punit_kind_idx = -1;

        if (dot) {
          *dot = '\0';
          d_idx = find_domain_index(target);
          if (d_idx >= 0) {
            punit_kind_idx = find_punit_kind_index(d_idx, dot + 1);
          }
        }

        if (d_idx >= 0 && punit_kind_idx >= 0) {
          // Free all punit configs that have this specific punit kind set
          punit_trace_config_t *prev = NULL;
          punit_trace_config_t *curr = domain_punit_trace_config[d_idx];
          int removed_count = 0;

          while (curr) {
            punit_trace_config_t *next = curr->next;

            if (curr->domain_punits[d_idx].punit[punit_kind_idx].set) {
              // Free bitsets
              for (int i = 0; i < num_domain; i++) {
                if (curr->domain_punits[i].set) {
                  for (int k = 0; k < MAX_NUM_PUNIT_KINDS; k++) {
                    if (curr->domain_punits[i].punit[k].set) {
                      bitset_free(&curr->domain_punits[i].punit[k].punit_ids);
                    }
                  }
                }
              }
              // Unlink and free
              if (prev) {
                prev->next = next;
              } else {
                domain_punit_trace_config[d_idx] = next;
              }
              free(curr);
              removed_count++;
            } else {
              prev = curr;
            }
            curr = next;
          }

          if (removed_count > 0) {
            fprintf(stderr,
                    "PInsight: Removed %d punit config(s) for %s.%s(*).\n",
                    removed_count, domain_info_table[d_idx].name,
                    domain_info_table[d_idx].punits[punit_kind_idx].name);
          }
        }
        current_section_type = SECTION_NONE;
        return 0;
      }
    }

    // Normal punit parsing: Domain.Kind(Set)
    // We need to parse this into a temp domain_punit_set_t to use for
    // matching/creating
    domain_punit_set_t temp_set[MAX_NUM_DOMAINS];
    memset(temp_set, 0, sizeof(temp_set)); // Important!

    if (parse_punit_set_string(target, temp_set) == 0) {
      // Find which domain is set (should be exactly one for the main target)
      int d_idx = -1;
      for (int i = 0; i < num_domain; i++) {
        if (temp_set[i].set) {
          d_idx = i;
          break;
        }
      }

      if (d_idx >= 0) {
        current_domain_idx = d_idx;

        // Find existing exact match
        punit_trace_config_t *existing =
            find_exact_punit_config(d_idx, &temp_set[d_idx]);
        punit_trace_config_t *config = NULL;

        if (existing) {
          config = existing;
          // REMOVE: clear events (effectively disable this punit config)
          if (action == ACTION_REMOVE) {
            config->events = 0;
            memset(config->domain_punits, 0, sizeof(config->domain_punits));
            // Re-apply the target constraint (it was cleared by memset)
            config->domain_punits[d_idx] = temp_set[d_idx];
          }
          // SET: merge — existing config stays, body will override specific
          // fields
        } else {
          // Create new if not found
          if (action != ACTION_REMOVE) {
            config = malloc(sizeof(punit_trace_config_t));
            memset(config, 0, sizeof(punit_trace_config_t));

            // Move temp_set to config
            config->domain_punits[d_idx] = temp_set[d_idx];

            // Link to the linked list of punit trace configs for this domain
            config->next = NULL;
            if (domain_punit_trace_config[d_idx] == NULL) {
              domain_punit_trace_config[d_idx] = config;
            } else {
              punit_trace_config_t *curr = domain_punit_trace_config[d_idx];
              while (curr->next)
                curr = curr->next;
              curr->next = config;
            }
          }
        }

        if (config) {
          current_punit_config = config;
          if (action == ACTION_REMOVE) {
            current_section_type = SECTION_NONE;
            // Events cleared above; no body to parse
            return 0;
          }

          // Apply Inheritance (Part 2)
          if (parts[0]) {
            char *inh_str = parts[0];
            char *d_dot = strstr(inh_str, ".default");
            if (d_dot) {
              *d_dot = '\0';
              int parent_idx = find_domain_index(inh_str);
              if (parent_idx >= 0)
                config->events = domain_default_trace_config[parent_idx].events;
            }
          }

          // Apply PunitSet (Part 3 - Constraints from other domains)
          if (parts[1]) {
            parse_punit_set_string(parts[1], config->domain_punits);
          }
        }
      }
    }
  }

  return 0;
}

static void parse_key_value(char *line) {
  char *eq = strchr(line, '=');
  if (!eq)
    return;

  *eq = '\0';
  char *key = trim_whitespace(line);
  char *val = trim_whitespace(eq + 1);

  if ((current_section_type == SECTION_LEXGION_ADDRESS ||
       current_section_type == SECTION_LEXGION_DEFAULT ||
       current_section_type == SECTION_LEXGION_DOMAIN_DEFAULT) &&
      current_lexgion_config) {
    // Determine how many configs to update
    int cfg_count = 1;
    lexgion_trace_config_t **cfgs = &current_lexgion_config;
    if (current_section_type == SECTION_LEXGION_ADDRESS &&
        num_current_lexgion_configs > 0) {
      cfg_count = num_current_lexgion_configs;
      cfgs = current_lexgion_configs;
    }

    // Parse the value once
    if (strcmp(key, "trace_starts_at") == 0) {
      int v = atoi(val);
      for (int ci = 0; ci < cfg_count; ci++)
        cfgs[ci]->trace_starts_at = v;
    } else if (strcmp(key, "max_num_traces") == 0) {
      int v = atoi(val);
      for (int ci = 0; ci < cfg_count; ci++)
        cfgs[ci]->max_num_traces = v;
    } else if (strcmp(key, "tracing_rate") == 0) {
      int v = atoi(val);
      for (int ci = 0; ci < cfg_count; ci++)
        cfgs[ci]->tracing_rate = v;
    } else if (strcmp(key, "trace_mode_after") == 0) {
      // Unified parsing for all trace_mode_after values (including PAUSE)
      trace_mode_after_t parsed;
      parse_trace_mode_after(val, &parsed);
      for (int ci = 0; ci < cfg_count; ci++)
        cfgs[ci]->mode_after = parsed;
    } else {
      // Check for Domain.Event override
      char *dot = strchr(key, '.');
      if (dot) {
        *dot = '\0';
        char *domain_name = key;
        char *event_name = dot + 1;
        int d_idx = find_domain_index(domain_name);
        if (d_idx >= 0) {
          struct domain_info *d = &domain_info_table[d_idx];
          int eid = -1;
          for (int k = 0; k < d->num_events; k++) {
            if (strcmp(d->event_table[k].name, event_name) == 0) {
              eid = k;
              break;
            }
          }
          if (eid != -1) {
            int enable = (strcasecmp(val, "on") == 0 || strcmp(val, "1") == 0);

            // Check installation status
            int installed = (d->eventInstallStatus >> eid) & 1;
            if (enable && !installed) {
              fprintf(stderr,
                      "PInSight Warning: Event '%s.%s' is enabled but not "
                      "installed (implemented). Ignoring and setting to OFF.\n",
                      d->name, d->event_table[eid].name);
              enable = 0;
            }

            for (int ci = 0; ci < cfg_count; ci++) {
              cfgs[ci]->domain_punits[d_idx].set = 1;
              cfgs[ci]->domain_events[d_idx].set = 1;
              if (enable)
                cfgs[ci]->domain_events[d_idx].events |= (1UL << eid);
              else
                cfgs[ci]->domain_events[d_idx].events &= ~(1UL << eid);
            }
          }
        }
        *dot = '.'; // restore
      }
    }
  } else if (current_section_type == SECTION_DOMAIN_GLOBAL &&
             current_domain_idx >= 0) {
    // --- Domain.global key-value parsing ---
    // Accepts: trace_mode, Domain.PunitKind = (Range)
    if (strcmp(key, "trace_mode") == 0) {
      if (strcasecmp(val, "OFF") == 0 || strcasecmp(val, "FALSE") == 0 ||
          strcmp(val, "0") == 0) {
        domain_default_trace_config[current_domain_idx].mode =
            PINSIGHT_DOMAIN_OFF;
      } else if (strcasecmp(val, "MONITORING") == 0 ||
                 strcasecmp(val, "MONITOR") == 0) {
        domain_default_trace_config[current_domain_idx].mode =
            PINSIGHT_DOMAIN_MONITORING;
      } else {
        /* ON, TRACING, TRUE, 1, or any unrecognized → full tracing */
        domain_default_trace_config[current_domain_idx].mode =
            PINSIGHT_DOMAIN_TRACING;
      }
    } else {
      // Check for Domain.PunitKind = (Range)
      // e.g. OpenMP.thread = (0-64)
      char *dot = strchr(key, '.');
      if (dot) {
        *dot = '\0';
        char *domain_name = key;
        char *punit_name = dot + 1;

        if (strcmp(domain_info_table[current_domain_idx].name, domain_name) ==
            0) {
          int p_idx = find_punit_kind_index(current_domain_idx, punit_name);
          if (p_idx >= 0) {
            // Parse value: (0, 128) or (0-128)
            char *p = val;
            while (*p && (*p == '(' || isspace((unsigned char)*p)))
              p++;

            char *end_ptr;
            long low = strtol(p, &end_ptr, 0);
            long high = low;

            // Check separator
            while (isspace((unsigned char)*end_ptr))
              end_ptr++;
            if (*end_ptr == ',' || *end_ptr == '-') {
              high = strtol(end_ptr + 1, NULL, 0);
            }

            domain_info_table[current_domain_idx].punits[p_idx].low = (int)low;
            domain_info_table[current_domain_idx].punits[p_idx].high =
                (int)high;
          }
        }
        *dot = '.'; // restore
      }
    }
  } else if (((current_section_type == SECTION_DOMAIN_DEFAULT &&
               current_domain_idx >= 0) ||
              (current_section_type == SECTION_DOMAIN_PUNIT &&
               current_punit_config)) &&
             current_domain_idx >= 0) {
    // --- Domain.default and Domain.punit key-value parsing ---
    // Both accept: EventName = on/off
    struct domain_info *d = &domain_info_table[current_domain_idx];
    int eid = -1;
    for (int k = 0; k < d->num_events; k++) {
      if (strcmp(d->event_table[k].name, key) == 0) {
        eid = k;
        break;
      }
    }
    if (eid != -1) {
      int enable = (strcasecmp(val, "on") == 0 || strcmp(val, "1") == 0);

      // Check installation status
      int installed = (d->eventInstallStatus >> eid) & 1;
      if (enable && !installed) {
        fprintf(stderr,
                "PInSight Warning: Event '%s.%s' is enabled but not "
                "installed (implemented). Ignoring and setting to OFF.\n",
                d->name, d->event_table[eid].name);
        enable = 0;
      }

      // Update the appropriate bitmask
      unsigned long *events_ptr;
      if (current_section_type == SECTION_DOMAIN_PUNIT) {
        punit_trace_config_t *pcfg =
            (punit_trace_config_t *)current_punit_config;
        events_ptr = &pcfg->events;
      } else {
        events_ptr = &domain_default_trace_config[current_domain_idx].events;
      }

      if (enable)
        *events_ptr |= (1UL << eid);
      else
        *events_ptr &= ~(1UL << eid);
    }
  } else if (current_section_type == SECTION_KNOB) {
    // --- Knob key-value parsing ---
    // Auto-detect type: try integer, then double, then string
    char *endptr;
    long lval = strtol(val, &endptr, 0);
    if (*endptr == '\0' && endptr != val) {
      // Integer value
      int idx = pinsight_find_or_create_knob(key, KNOB_TYPE_INT);
      if (idx >= 0)
        pinsight_set_knob_int(idx, (int)lval);
    } else {
      double dval = strtod(val, &endptr);
      if (*endptr == '\0' && endptr != val) {
        // Double value
        int idx = pinsight_find_or_create_knob(key, KNOB_TYPE_DOUBLE);
        if (idx >= 0)
          pinsight_set_knob_double(idx, dval);
      } else {
        // String value
        int idx = pinsight_find_or_create_knob(key, KNOB_TYPE_STRING);
        if (idx >= 0)
          pinsight_set_knob_string(idx, val);
      }
    }
  }
}

// --- Helper Implementations ---

static void reset_domain_default_config(int domain_idx) {
  if (domain_idx < 0 || domain_idx >= num_domain)
    return;

  // Reset events to installed defaults
  domain_default_trace_config[domain_idx].events =
      domain_info_table[domain_idx].eventInstallStatus;
  domain_default_trace_config[domain_idx].mode =
      domain_info_table[domain_idx].starting_mode;
}

static void reset_lexgion_config(lexgion_trace_config_t *lg) {
  if (!lg)
    return;
  void *saved_ptr = lg->codeptr;
  // Copy defaults from global lexgion default
  *lg = *lexgion_default_trace_config;
  lg->codeptr = saved_ptr; // Restore codeptr
}

static punit_trace_config_t *
find_exact_punit_config(int domain_idx, domain_punit_set_t *target_set) {
  punit_trace_config_t *curr = domain_punit_trace_config[domain_idx];
  while (curr) {
    // Identity Match: Same Domain (implicit by list), Same Punit Kind, Exact
    // Punit Set Check ONLY the primary domain constraint (domain_idx)
    if (curr->domain_punits[domain_idx].set && target_set[domain_idx].set) {
      // Check kinds
      int match = 1;
      for (int k = 0; k < MAX_NUM_PUNIT_KINDS; k++) {
        int s1 = curr->domain_punits[domain_idx].punit[k].set;
        int s2 = target_set[domain_idx].punit[k].set;

        if (s1 != s2) {
          match = 0;
          break;
        }
        if (s1) {
          if (!bitset_equal(&curr->domain_punits[domain_idx].punit[k].punit_ids,
                            &target_set[domain_idx].punit[k].punit_ids)) {
            match = 0;
            break;
          }
        }
      }
      if (match)
        return curr;
    }
    curr = curr->next;
  }
  return NULL;
}

int find_domain_index(const char *name) {
  for (int i = 0; i < num_domain; i++) {
    if (strcmp(domain_info_table[i].name, name) == 0)
      return i;
  }
  return -1;
}

static int find_punit_kind_index(int domain_idx, const char *punit_name) {
  struct domain_info *d = &domain_info_table[domain_idx];
  for (int i = 0; i < d->num_punits; i++) {
    if (strcmp(d->punits[i].name, punit_name) == 0)
      return i;
  }
  return -1;
}

// punit string format is "domain.punit(1, 3, 4-10, 12, 15-18),
// domain2.punit2(1, 3, 4-10, 12, 15-18)"
static int parse_punit_set_string(char *spec_str,
                                  domain_punit_set_t *set_array) {
  char *ptr = spec_str;
  while (*ptr) {
    // Skip leading dots/spaces
    while (*ptr == '.' || isspace((unsigned char)*ptr))
      ptr++;
    if (!*ptr)
      break;

    // Parse Domain
    char domain_name[32];
    char *dot = strchr(ptr, '.');
    if (!dot)
      return -1;

    int len = dot - ptr;
    if (len >= sizeof(domain_name))
      len = sizeof(domain_name) - 1;
    strncpy(domain_name, ptr, len);
    domain_name[len] = '\0';
    ptr = dot + 1;

    // Find Domain Index
    int d_idx = find_domain_index(domain_name);

    // Parse Punit Kind
    char punit_kind[32];
    char *paren = strchr(ptr, '(');
    if (!paren)
      return -1;

    len = paren - ptr;
    if (len >= sizeof(punit_kind))
      len = sizeof(punit_kind) - 1;
    strncpy(punit_kind, ptr, len);
    punit_kind[len] = '\0';
    ptr = paren + 1;

    // Find Punit Kind Index
    int p_idx = -1;
    if (d_idx >= 0) {
      p_idx = find_punit_kind_index(d_idx, punit_kind);
    }

    // Parse Range String inside (...)
    char *paren_end = strchr(ptr, ')');
    if (!paren_end)
      return -1;
    *paren_end = '\0';

    // Ptr now points to "1, 3, 4-10..."

    // Populate Struct
    if (d_idx >= 0 && p_idx >= 0) {

      // Initialize bitset only if not already set (to avoid leak)
      if (!set_array[d_idx].punit[p_idx].set) {
        bitset_init(&set_array[d_idx].punit[p_idx].punit_ids, 1024);
      }

      set_array[d_idx].set = 1;
      set_array[d_idx].punit[p_idx].set = 1;

      // Parse ranges into the bitset
      if (bitset_parse_ranges(&set_array[d_idx].punit[p_idx].punit_ids, ptr) !=
          0) {
        fprintf(stderr,
                "PInSight Error: Invalid punit range specification: %s\n", ptr);
        return -1;
      }

      // Validate Punit Range
      struct domain_info *d = &domain_info_table[d_idx];
      int limit_low = d->punits[p_idx].low;
      int limit_high = d->punits[p_idx].high;

      // Check max set bit
      int max_bit = -1;
      // bitset doesn't implement get_max_set_bit inefficiently traverse?
      // Actually, we can check during iteration or just rely on the fact that
      // if a bit is set > limit, it's bad. Simplified check: iterate checking
      // bits? Better: modify bitset.c or just iterate high-low if possible.
      // Given bitset structure isn't fully visible here (opaque pointers
      // usually), we rely on public API. pinsight.h defines bitset as struct
      // with 'size' and 'bits'. If visible: But we can check if any bit < low
      // or > high is set. Since bitset_parse_ranges handles formatting, we just
      // need to validate constraints. Actually, `bitset_parse_ranges` might not
      // check limits? "If a punit specified ... must be within range ... If out
      // of range, report warning and ignore." Ignoring means unsetting the bit?

      // Let's implement a simple check loop using proper API
      // Assuming max range isn't huge (usually < 1024 as per init).
      for (int k = 0; k < 1024; k++) { // Max bitset size
        if (bitset_test(&set_array[d_idx].punit[p_idx].punit_ids, k)) {
          if (k < limit_low || k > limit_high) {
            fprintf(stderr,
                    "PInSight Warning: Punit ID %d for %s.%s is out of valid "
                    "range (%d-%d). Ignoring.\n",
                    k, d->name, d->punits[p_idx].name, limit_low, limit_high);
            // Unset it
            bitset_clear(&set_array[d_idx].punit[p_idx].punit_ids, k);
          }
        }
      }
    }

    *paren_end = ')'; // restore
    ptr = paren_end + 1;

    // Skip comma if present
    while (isspace((unsigned char)*ptr))
      ptr++;
    if (*ptr == ',')
      ptr++;
    else if (*ptr != '\0') {
      // Garbage or invalid format found, e.g. .thread without domain
      return -1;
    }
  }
  return 0;
}

// Replaces SpecItem access logic with direct struct populating
static int apply_inheritance(lexgion_trace_config_t *lg_config,
                             char *inheritance_str) {
  char *copy = strdup(inheritance_str);
  char *token = strtok(copy, ",");
  while (token) {
    char *name = trim_whitespace(token);
    char *dot = strchr(name, '.');
    if (!dot) {
      fprintf(stderr,
              "PInsight Warning: Invalid inheritance '%s'; "
              "only Domain.default is supported. Ignoring.\n",
              name);
      token = strtok(NULL, ",");
      continue;
    }
    if (strcmp(dot + 1, "default") != 0) {
      fprintf(stderr,
              "PInsight Warning: Invalid inheritance '%s'; "
              "only Domain.default is supported. Ignoring.\n",
              name);
      token = strtok(NULL, ",");
      continue;
    }
    *dot = '\0';
    int idx = find_domain_index(name);
    if (idx < 0) {
      fprintf(stderr,
              "PInsight Warning: Unknown domain '%s' in inheritance. "
              "Ignoring.\n",
              name);
      token = strtok(NULL, ",");
      continue;
    }
    lg_config->domain_punits[idx].set = 1;
    lg_config->domain_events[idx].set = 1;
    lg_config->domain_events[idx].events =
        domain_default_trace_config[idx].events;
    token = strtok(NULL, ",");
  }
  free(copy);
  return 0;
}

static lexgion_trace_config_t *get_or_create_lexgion_config(void *codeptr) {
  for (int i = 0; i < num_lexgion_address_trace_configs; i++) {
    if (lexgion_address_trace_config[i].codeptr == codeptr) {
      return &lexgion_address_trace_config[i];
    }
  }

  if (num_lexgion_address_trace_configs < MAX_NUM_LEXGIONS) {
    lexgion_trace_config_t *lg =
        &lexgion_address_trace_config[num_lexgion_address_trace_configs++];
    // Initialize from Lexgion.default so new entries inherit default rate,
    // max_num_traces, trace_starts_at, mode_after, etc.
    *lg = *lexgion_default_trace_config;
    lg->codeptr = codeptr;
    lg->removed = 0;
    return lg;
  }
  return NULL;
}
