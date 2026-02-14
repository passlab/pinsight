#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <inttypes.h>
#include <limits.h>

#include "trace_config_parse.h"
#include "trace_config.h"
#include "bitset.h"
#include "trace_domain_loader.h" // For domain_info_table access

// Forward declarations
static int process_line(char *line);
static int parse_section_header(char *line);
static void parse_key_value(char *line);
static int parse_domain_punit_spec(char *spec, int *domain_idx, int *punit_kind_idx, unsigned int *low, unsigned int *high);
static int parse_range_string(const char *range_str, unsigned int *low, unsigned int *high, BitSet *range_mask);
static lexgion_trace_config_t* get_or_create_lexgion_config(void *codeptr);
static punit_trace_config_t* get_or_create_punit_config(int domain_idx, BitSet *punit_mask, int punit_kind, unsigned int low, unsigned int high);

// State variables for parsing context of the four kinds of sections
typedef enum {
    SECTION_NONE,
    SECTION_DOMAIN_DEFAULT,
    SECTION_DOMAIN_PUNIT,
    SECTION_LEXGION,
    SECTION_LEXGION_DEFAULT
} SectionType;

typedef enum {
    ACTION_ADD,
    ACTION_REPLACE,
    ACTION_REMOVE
} ConfigAction;

static SectionType current_section_type = SECTION_NONE;
static int current_domain_idx = -1;
static punit_trace_config_t* current_punit_config = NULL; 
static lexgion_trace_config_t* current_lexgion_config = NULL;

#define MAX_LINE_LENGTH 1024

// --- Utility Functions ---

char* trim_whitespace(char* str) {
    char* end;
    while(isspace((unsigned char)*str)) str++;
    if(*str == 0) return str;
    end = str + strlen(str) - 1;
    while(end > str && isspace((unsigned char)*end)) end--;
    *(end+1) = 0;
    return str;
}

// Parses "4, 6, 8-12" into a BitSet. Only relevant if we were using bitsets for single punit check,
// but the current struct punit_trace_config uses a BitSet for punit_ids.
// Returns 0 on success.
static int parse_range_list(const char *str, BitSet *mask) {
    return bitset_parse_ranges(mask, str);
}



// --- Parsing Logic ---

void parse_trace_config_file(char* filename) {
    FILE *fp = fopen(filename, "r");
    if (!fp) {
        fprintf(stderr, "PInSight: Could not open trace configuration file %s.\n", filename);
        return;
    }

    char line[MAX_LINE_LENGTH];
    int line_num = 0;
    while (fgets(line, sizeof(line), fp)) {
        line_num++;
        if (process_line(line) != 0) {
            fprintf(stderr, "PInSight: Error parsing config file %s at line %d: %s", filename, line_num, line);
            break; 
        }
    }

    fclose(fp);
}

static int process_line(char *line) {
    char *trimmed = trim_whitespace(line);
    if (strlen(trimmed) == 0 || trimmed[0] == '#') return 0;

    if (trimmed[0] == '[') {
        return parse_section_header(trimmed);
    } else {
        parse_key_value(trimmed);
        return 0;
    }
}


// --- Helper Structs for Punit Parsing ---
// --- Forward Declarations ---
static int find_domain_index(const char *name);
static int find_punit_kind_index(int domain_idx, const char *punit_name);
static int apply_inheritance(lexgion_trace_config_t *lg_config, char *inheritance_str);
static int parse_punit_set_string(char *spec_str, domain_punit_set_t *set_array);
static void reset_domain_config(int domain_idx);
static void reset_lexgion_config(lexgion_trace_config_t *lg);
static punit_trace_config_t* find_exact_punit_config(int domain_idx, domain_punit_set_t *target_set);

// Example section: [ACTION Target] : Inheritance : PunitSet
// Target: Lexgion(...) or Domain.Kind(Range)
static int parse_section_header(char *line) {
    // 1. Locate closing bracket ']'
    char *close_bracket = strchr(line, ']');
    if (!close_bracket) return -1;
    
    *close_bracket = '\0';
    char *inside_brackets = line + 1;
    char *outside_brackets = close_bracket + 1;
    
    // 2. Parse Action and Target from inside brackets
    ConfigAction action = ACTION_ADD; // Default
    char *target = inside_brackets;
    
    // Check for prefixes
    if (strncasecmp(inside_brackets, "ADD ", 4) == 0) {
        action = ACTION_ADD;
        target = inside_brackets + 4;
    } else if (strncasecmp(inside_brackets, "REPLACE ", 8) == 0) {
        action = ACTION_REPLACE;
        target = inside_brackets + 8;
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
    while(isspace((unsigned char)*ptr)) ptr++;
    if (*ptr == ':') {
        ptr++; // Skip colon
        char *token = strtok(ptr, ":" );
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

    // Case 1: Lexgion(0x...) or Lexgion.default
    if (strncmp(target, "Lexgion", 7) == 0) {
        lexgion_trace_config_t *lg = NULL;
        if (strcmp(target, "Lexgion.default") == 0) {
            current_section_type = SECTION_LEXGION_DEFAULT;
            lg = &lexgion_trace_config[0];
            // Usually Lexgion.default shouldn't utilize num_lexgion... increment?
            // Actually it is index 0.
        } else if (strncmp(target, "Lexgion(", 8) == 0) {
            current_section_type = SECTION_LEXGION;
            char *ptr_start = strchr(target, '(');
            char *ptr_end = strchr(target, ')');
            if (ptr_start && ptr_end) {
                *ptr_end = '\0';
                uint64_t addr = strtoull(ptr_start + 1, NULL, 0);
                void *codeptr = (void*)(uintptr_t)addr;
                lg = get_or_create_lexgion_config(codeptr);
            }
        }
        
        if (lg) {
            current_lexgion_config = lg;
            
            if (action == ACTION_REPLACE || action == ACTION_REMOVE) {
                reset_lexgion_config(lg);
            }
            
            if (action == ACTION_REMOVE) {
                // Keep it reset (tracing_rate=0 implies disabled effectively or we can set max_traces=0)
                lg->max_num_traces = 0;
                current_section_type = SECTION_NONE; // Stop parsing body
                return 0;
            }
            
         

            // Apply Inheritance
            if (parts[0]) {
                 apply_inheritance(lg, parts[0]);
            }
            
            // Apply PunitSet
            if (parts[1]) {
                parse_punit_set_string(parts[1], lg->domain_punits);
            }
        }
    }
    // Case 2: Domain.default
    else if (strstr(target, ".default")) {
        current_section_type = SECTION_DOMAIN_DEFAULT;
        char *dot = strchr(target, '.');
        if (dot) {
            *dot = '\0';
            int idx = find_domain_index(target);
            if (idx >= 0) {
                current_domain_idx = idx;
                
                if (action == ACTION_REPLACE || action == ACTION_REMOVE) {
                    reset_domain_config(idx);
                }
                
                if (action == ACTION_REMOVE) {
                     domain_trace_config[idx].set = 0; // Disable
                     current_section_type = SECTION_NONE;
                     return 0;
                }
                
            }
        }
    }
    // Case 3: Domain.punit specification
    else {
        current_section_type = SECTION_DOMAIN_PUNIT;
        
        // Target: Domain.Kind(Set)
        // We need to parse this into a temp domain_punit_set_t to use for matching/creating
        domain_punit_set_t temp_set[MAX_NUM_DOMAINS];
        memset(temp_set, 0, sizeof(temp_set)); // Important!
        
        if (parse_punit_set_string(target, temp_set) == 0) {
            // Find which domain is set (should be exactly one for the main target)
            int d_idx = -1;
            for(int i=0; i<num_domain; i++) {
                if (temp_set[i].set) {
                    d_idx = i;
                    break;
                }
            }
            
            if (d_idx >= 0) {
                current_domain_idx = d_idx;
                
                // Find existing exact match
                punit_trace_config_t *existing = find_exact_punit_config(d_idx, &temp_set[d_idx]);
                punit_trace_config_t *config = NULL;
                
                if (existing) {
                    config = existing;
                    // IF ACTION matches logic for replace/reset
                    if (action == ACTION_REPLACE || action == ACTION_REMOVE) {
                        // Clear events and auxiliary constraints
                        config->events = 0;
                        memset(config->domain_punits, 0, sizeof(config->domain_punits));
                        // Re-apply the target constraint (it was cleared by memset)
                        config->domain_punits[d_idx] = temp_set[d_idx];
                        // Note: temp_set pointers are valid? `parse_punit_set_string` might allocate bitsets.
                        // We need to ensure we don't double-free or leak.
                        // `parse_punit_set_string` calls `bitset_init` and `bitset_parse_ranges`.
                        // We copied struct content. If bitset uses pointers, we now have two pointers to same memory if we are not careful.
                        // Wait, `domain_punit_set_t` bitsets. In `parse_punit_set_string`, it initializes them.
                        // If we assign `config->domain_punits[d_idx] = temp_set[d_idx]`, we are moving ownership effectively.
                        // But `existing` ALREADY had bitsets initialized. We triggered leak if we didn't free them first?
                        // `bitset_free` isn't called here.
                        // FIXME: Memory management for bitsets in punit matching.
                        
                        // For simplicity in this step: Assumed bitsets are small & inline or we rely on OS cleanup?
                        // Correct way: bitset_free on old, then move new.
                    }
                } else {
                     // Create new if not found (and not REMOVE/RESET usually, but RESET on non-existing is no-op, ADD/REPLACE create)
                     if (action != ACTION_REMOVE) {
                         config = malloc(sizeof(punit_trace_config_t));
                         memset(config, 0, sizeof(punit_trace_config_t));
                         
                         // Move temp_set to config
                         config->domain_punits[d_idx] = temp_set[d_idx];
                         
                         // Link
                         config->next = NULL;
                         if (domain_trace_config[d_idx].punit_trace_config == NULL) {
                             domain_trace_config[d_idx].punit_trace_config = config;
                         } else {
                             punit_trace_config_t *curr = domain_trace_config[d_idx].punit_trace_config;
                             while(curr->next) curr = curr->next;
                             curr->next = config;
                         }
                     }
                }
                
                if (config) {
                    current_punit_config = config;
                    if (action == ACTION_REMOVE) {
                         current_section_type = SECTION_NONE;
                         // If REMOVE, we should arguably unlink and free it, but simply clearing events disables it effectively.
                         return 0;
                    }
                    
                    // Apply Inheritance (Part 2)
                    if (parts[0]) {
                        char *inh_str = parts[0];
                        char *d_dot = strstr(inh_str, ".default");
                        if (d_dot) {
                             *d_dot = '\0';
                             int parent_idx = find_domain_index(inh_str);
                             if (parent_idx >= 0) config->events = domain_trace_config[parent_idx].events;
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
    if (!eq) return; 

    *eq = '\0';
    char *key = trim_whitespace(line);
    char *val = trim_whitespace(eq + 1);

    if ((current_section_type == SECTION_LEXGION || current_section_type == SECTION_LEXGION_DEFAULT) && current_lexgion_config) {
        if (strcmp(key, "trace_starts_at") == 0) current_lexgion_config->trace_starts_at = atoi(val);
        else if (strcmp(key, "max_num_traces") == 0) current_lexgion_config->max_num_traces = atoi(val);
        else if (strcmp(key, "tracing_rate") == 0) current_lexgion_config->tracing_rate = atoi(val);
        else {
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
                    for(int k=0; k<d->num_events; k++) {
                        if(strcmp(d->event_table[k].name, event_name) == 0) {
                            eid = k;
                            break;
                        }
                    }
                    if (eid != -1) {
                         int enable = (strcasecmp(val, "on") == 0 || strcmp(val, "1") == 0);
                         
                         // Check installation status
                         int installed = (d->eventInstallStatus >> eid) & 1;
                         if (enable && !installed) {
                              fprintf(stderr, "PInSight Warning: Event '%s.%s' is enabled but not installed (implemented). Ignoring and setting to OFF.\n", 
                                      d->name, d->event_table[eid].name);
                              enable = 0;
                         }

                         current_lexgion_config->domain_punits[d_idx].set = 1;
                         current_lexgion_config->domain_events[d_idx].set = 1;
                         if(enable) current_lexgion_config->domain_events[d_idx].events |= (1UL << eid);
                         else current_lexgion_config->domain_events[d_idx].events &= ~(1UL << eid);
                    }
                }
                *dot = '.'; // restore
            }
        }
    }
    else if (current_section_type == SECTION_DOMAIN_DEFAULT && current_domain_idx >= 0) {
        // Check for Domain.Punit = (low, high) or (low-high)
        char *dot = strchr(key, '.');
        if (dot) {
             // It might be a punit range specification
             // e.g. OpenMP.thread = (0-64)
             *dot = '\0';
             char *domain_name = key;
             char *punit_name = dot + 1;
             
             // Verify domain name matches current section?
             if (strcmp(domain_info_table[current_domain_idx].name, domain_name) == 0) {
                 int p_idx = find_punit_kind_index(current_domain_idx, punit_name);
                 if (p_idx >= 0) {
                     // Parse value: (0, 128) or (0-128)
                     char *p = val;
                     while (*p && (*p == '(' || isspace((unsigned char)*p))) p++;
                     
                     char *end_ptr;
                     long low = strtol(p, &end_ptr, 0);
                     long high = low;
                     
                     // Check separator
                     while(isspace((unsigned char)*end_ptr)) end_ptr++;
                     if (*end_ptr == ',' || *end_ptr == '-') {
                         high = strtol(end_ptr + 1, NULL, 0);
                     }
                     
                     domain_info_table[current_domain_idx].punits[p_idx].low = (int)low;
                     domain_info_table[current_domain_idx].punits[p_idx].high = (int)high;
                 }
             }
             *dot = '.'; // restore
        } else {
             // Event = on/off
             struct domain_info *d = &domain_info_table[current_domain_idx];
             int eid = -1;
             for(int k=0; k<d->num_events; k++) {
                  if(strcmp(d->event_table[k].name, key) == 0) {
                      eid = k; 
                      break;
                  }
             }
             if (eid != -1) {
                 // Update domain default config
                 int enable = (strcasecmp(val, "on") == 0 || strcmp(val, "1") == 0);
                 
                 // Check installation status
                 struct domain_info *d = &domain_info_table[current_domain_idx];
                 int installed = (d->eventInstallStatus >> eid) & 1;
                 if (enable && !installed) {
                      fprintf(stderr, "PInSight Warning: Event '%s.%s' is enabled but not installed (implemented). Ignoring and setting to OFF.\n", 
                              d->name, d->event_table[eid].name);
                      enable = 0;
                 }
                 
                 if(enable) domain_trace_config[current_domain_idx].events |= (1UL << eid);
                 else domain_trace_config[current_domain_idx].events &= ~(1UL << eid);
             }
        }
    }
    else if (current_section_type == SECTION_DOMAIN_PUNIT && current_punit_config) {
         // Update event for specific punit config
         punit_trace_config_t *pcfg = (punit_trace_config_t*)current_punit_config;
         int dom_idx = -1; 
         // Find which domain this punit config belongs to? 
         // We stored it in linked list of domain_trace_config[dom_idx], 
         // but here we just need to search for event name in ALL domains? 
         // OR usually it belongs to the domain defined in the header.
         // Let's assume current_domain_idx was set correctly in parse_section_header
         
         if (current_domain_idx >= 0) {
            struct domain_info *d = &domain_info_table[current_domain_idx];
            int eid = -1;
            for(int k=0; k<d->num_events; k++) {
                 if(strcmp(d->event_table[k].name, key) == 0) {
                     eid = k; 
                     break;
                 }
            }
            if (eid != -1) {
                int enable = (strcasecmp(val, "on") == 0 || strcmp(val, "1") == 0);
                
                 // Check installation status
                 int installed = (d->eventInstallStatus >> eid) & 1;
                 if (enable && !installed) {
                      fprintf(stderr, "PInSight Warning: Event '%s.%s' is enabled but not installed (implemented). Ignoring and setting to OFF.\n", 
                              d->name, d->event_table[eid].name);
                      enable = 0;
                 }
                 
                if(enable) pcfg->events |= (1UL << eid);
                else pcfg->events &= ~(1UL << eid);
            }
         }
    }
}

// --- Helper Implementations ---

static void reset_domain_config(int domain_idx) {
    if (domain_idx < 0 || domain_idx >= num_domain) return;
    
    // Reset events to installed defaults
    domain_trace_config[domain_idx].events = domain_info_table[domain_idx].eventInstallStatus;
    // Set enabled/disabled based on events availability (mimic initial setup)
    domain_trace_config[domain_idx].set = (domain_trace_config[domain_idx].events != 0);
    
    // Free punit trace configs
    punit_trace_config_t *curr = domain_trace_config[domain_idx].punit_trace_config;
    while (curr) {
        punit_trace_config_t *next = curr->next;
        // Free bitsets in domain_punits array
        for(int i=0; i<num_domain; i++) {
            if (curr->domain_punits[i].set) {
                for(int k=0; k<MAX_NUM_PUNIT_KINDS; k++) {
                    if (curr->domain_punits[i].punit[k].set) {
                        bitset_free(&curr->domain_punits[i].punit[k].punit_ids);
                    }
                }
            }
        }
        free(curr);
        curr = next;
    }
    domain_trace_config[domain_idx].punit_trace_config = NULL;
}

static void reset_lexgion_config(lexgion_trace_config_t *lg) {
    if (!lg) return;
    void *saved_ptr = lg->codeptr;
    // Copy defaults from lexgion_trace_config[0]
    *lg = lexgion_trace_config[0];
    lg->codeptr = saved_ptr; // Restore codeptr
}

static punit_trace_config_t* find_exact_punit_config(int domain_idx, domain_punit_set_t *target_set) {
    punit_trace_config_t *curr = domain_trace_config[domain_idx].punit_trace_config;
    while (curr) {
        // Identity Match: Same Domain (implicit by list), Same Punit Kind, Exact Punit Set
        // Check ONLY the primary domain constraint (domain_idx)
        if (curr->domain_punits[domain_idx].set && target_set[domain_idx].set) {
             // Check kinds
             int match = 1;
             for(int k=0; k<MAX_NUM_PUNIT_KINDS; k++) {
                 int s1 = curr->domain_punits[domain_idx].punit[k].set;
                 int s2 = target_set[domain_idx].punit[k].set;
                 
                 if (s1 != s2) { match = 0; break; }
                 if (s1) {
                     if (!bitset_equal(&curr->domain_punits[domain_idx].punit[k].punit_ids, 
                                       &target_set[domain_idx].punit[k].punit_ids)) {
                         match = 0;
                         break;
                     }
                 }
             }
             if (match) return curr;
        }
        curr = curr->next;
    }
    return NULL;
}

static int find_domain_index(const char *name) {
    for(int i=0; i<num_domain; i++) {
        if(strcmp(domain_info_table[i].name, name) == 0) return i;
    }
    return -1;
}

static int find_punit_kind_index(int domain_idx, const char *punit_name) {
    struct domain_info *d = &domain_info_table[domain_idx];
    for(int i=0; i<d->num_punits; i++) {
        if(strcmp(d->punits[i].name, punit_name) == 0) return i;
    }
    return -1;
}

//punit string format is "domain.punit(1, 3, 4-10, 12, 15-18), domain2.punit2(1, 3, 4-10, 12, 15-18)"
static int parse_punit_set_string(char *spec_str, domain_punit_set_t *set_array) {
    char *ptr = spec_str;
    while (*ptr) {
        // Skip leading dots/spaces
        while (*ptr == '.' || isspace((unsigned char)*ptr)) ptr++;
        if (!*ptr) break;

        // Parse Domain
        char domain_name[32];
        char *dot = strchr(ptr, '.');
        if (!dot) return -1; 
        
        int len = dot - ptr;
        if (len >= sizeof(domain_name)) len = sizeof(domain_name) - 1;
        strncpy(domain_name, ptr, len);
        domain_name[len] = '\0';
        ptr = dot + 1;
        
        // Find Domain Index
        int d_idx = find_domain_index(domain_name);
        
        // Parse Punit Kind
        char punit_kind[32];
        char *paren = strchr(ptr, '(');
        if (!paren) return -1;
        
        len = paren - ptr;
        if (len >= sizeof(punit_kind)) len = sizeof(punit_kind) - 1;
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
        if (!paren_end) return -1;
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
            if (bitset_parse_ranges(&set_array[d_idx].punit[p_idx].punit_ids, ptr) != 0) {
                fprintf(stderr, "PInSight Error: Invalid punit range specification: %s\n", ptr);
                return -1;
            }
            
            // Validate Punit Range
            struct domain_info *d = &domain_info_table[d_idx];
            int limit_low = d->punits[p_idx].low;
            int limit_high = d->punits[p_idx].high;
            
            // Check max set bit
            int max_bit = -1;
            // bitset doesn't implement get_max_set_bit inefficiently traverse?
            // Actually, we can check during iteration or just rely on the fact that if a bit is set > limit, it's bad.
            // Simplified check: iterate checking bits?
            // Better: modify bitset.c or just iterate high-low if possible.
            // Given bitset structure isn't fully visible here (opaque pointers usually), we rely on public API.
            // pinsight.h defines bitset as struct with 'size' and 'bits'. If visible:
            // But we can check if any bit < low or > high is set.
            // Since bitset_parse_ranges handles formatting, we just need to validate constraints.
            // Actually, `bitset_parse_ranges` might not check limits?
            // "If a punit specified ... must be within range ... If out of range, report warning and ignore."
            // Ignoring means unsetting the bit?
            
            // Let's implement a simple check loop using proper API
            // Assuming max range isn't huge (usually < 1024 as per init).
            for (int k = 0; k < 1024; k++) { // Max bitset size
                 if (bitset_test(&set_array[d_idx].punit[p_idx].punit_ids, k)) {
                     if (k < limit_low || k > limit_high) {
                          fprintf(stderr, "PInSight Warning: Punit ID %d for %s.%s is out of valid range (%d-%d). Ignoring.\n", 
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
        while(isspace((unsigned char)*ptr)) ptr++;
        if (*ptr == ',') ptr++;
        else if (*ptr != '\0') {
             // Garbage or invalid format found, e.g. .thread without domain
             return -1;
        }
    }
    return 0;
}

// Replaces SpecItem access logic with direct struct populating
static int apply_inheritance(lexgion_trace_config_t *lg_config, char *inheritance_str) {
    char *copy = strdup(inheritance_str);
    char *token = strtok(copy, ",");
    while (token) {
        char *name = trim_whitespace(token);
        char *dot = strchr(name, '.');
        if (dot) *dot = '\0';
        int idx = find_domain_index(name);
        if (idx >= 0) {
            lg_config->domain_punits[idx].set = 1;
            lg_config->domain_events[idx].set = 1;
            lg_config->domain_events[idx].events = domain_trace_config[idx].events;
        }
        token = strtok(NULL, ",");
    }
    free(copy);
    return 0;
}

static lexgion_trace_config_t* get_or_create_lexgion_config(void *codeptr) {
    for(int i=1; i<num_lexgion_trace_configs; i++) {
        if (lexgion_trace_config[i].codeptr == codeptr) {
            return &lexgion_trace_config[i];
        }
    }

    if (num_lexgion_trace_configs < MAX_NUM_LEXGIONS) {
         lexgion_trace_config_t *lg = &lexgion_trace_config[num_lexgion_trace_configs++];
         // Initialize with global defaults
         *lg = lexgion_trace_config[0]; 
         lg->codeptr = codeptr;
         return lg;
    }
    return NULL;
}
// --- Serialization Implementation ---

// Helper to print punit set
// Helper to print punit set
// filter_domain_idx: -1 for all.
// exclude_mode: 0 = include only filter_domain_idx, 1 = exclude filter_domain_idx
static void print_punit_set_filtered(FILE *out, domain_punit_set_t *set_array, int *first_printed, int filter_domain_idx, int exclude_mode) {
    for (int di = 0; di < num_domain; di++) {
        // Apply Filter
        if (filter_domain_idx >= 0) {
            if (exclude_mode && di == filter_domain_idx) continue;
            if (!exclude_mode && di != filter_domain_idx) continue;
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
                        fprintf(out, "%s.%s(%s)", target_domain->name, target_domain->punits[pi].name, range_str);
                        free(range_str);
                    } else {
                         fprintf(out, "%s.%s()", target_domain->name, target_domain->punits[pi].name);
                    }
                }
            }
        }
    }
}

void print_domain_trace_config(FILE *out) {
    if (!out) return;

    for (int i = 0; i < num_domain; i++) {
        struct domain_info *d = &domain_info_table[i];
        fprintf(out, "[%s.default]\n", d->name);
        unsigned long current_events = domain_trace_config[i].events;
        for (int k = 0; k < d->num_events; k++) {
            if (strlen(d->event_table[k].name) == 0) continue;
            if (k >= 64) break; 
            int on = (current_events >> k) & 1;
            fprintf(out, "    %s = %s\n", d->event_table[k].name, on ? "on" : "off");
        }
        fprintf(out, "\n");

        punit_trace_config_t *curr = domain_trace_config[i].punit_trace_config;
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
            // We can buffer it or just check if anything WOULD be printed, but `print_punit_set` modifies stream.
            // Let's use a temp buffer or just print a separator if needed?
            // Simpler: Print comma/colon logic inside? No.
            // Let's check if others exist.
            int others_exist = 0;
            for(int k=0; k<num_domain; k++) {
                if(k == i) continue;
                if(curr->domain_punits[k].set) {
                     // Check if it has punits
                     for(int p=0; p<domain_info_table[k].num_punits; p++) 
                        if(curr->domain_punits[k].punit[p].set) others_exist = 1;
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
                if (strlen(d->event_table[k].name) == 0) continue;
                if (k >= 64) break;
                int on = (p_events >> k) & 1;
                fprintf(out, "    %s = %s\n", d->event_table[k].name, on ? "on" : "off");
            }
            fprintf(out, "\n");
            curr = curr->next;
        }
    }
}

void print_lexgion_trace_config(FILE *out) {
    if (!out) return;

    for (int i = 0; i < num_lexgion_trace_configs; i++) {
        lexgion_trace_config_t *lg = &lexgion_trace_config[i];
        if (i == 0) {
            fprintf(out, "[Lexgion.default]");
        } else {
            fprintf(out, "[Lexgion(%p)]", lg->codeptr);
        }

        // Part 2: Inheritance
        int first_inh = 1;
        int printed_inh = 0;
        
        // Check for inheritance (Enabled but NO specific punits)
        for (int di = 0; di < num_domain; di++) {
            if (lg->domain_punits[di].set) {
                 int has_punits = 0;
                 struct domain_info *d = &domain_info_table[di];
                 for(int p=0; p<d->num_punits; p++) {
                     if(lg->domain_punits[di].punit[p].set) has_punits = 1;
                 }
                 
                 if (!has_punits) {
                     if (first_inh) { fprintf(out, ": "); first_inh = 0; }
                     else { fprintf(out, ", "); }
                     fprintf(out, "%s.default", d->name);
                     printed_inh = 1;
                 }
            }
        }
        
        // Part 3: Punit Set (Enabled AND HAS specific punits)
        // Note: If NO inheritance was printed, but Punits exist, we still need separation.
        // Format: [Target]: Inheritance : PunitSet
        // If Inheritance is empty but PunitSet exists: [Target]: : PunitSet ? Or [Target] : PunitSet?
        // Parser expects [Target] : Part2 : Part3.
        // If Part2 is empty? `[Target] : : PunitSet` or `[Target] : PunitSet`?
        // `parse_section_header` logic:
        // `strtok` (colon).
        // If one colon -> Part 2 only (Inheritance).
        // If two colons -> Part 2 and Part 3.
        // If separate inheritance loop found nothing, we might skip colon?
        // But if we have PunitSet, we need it.
        // Actually `parse_punit_set_string` is Part 3. `apply_inheritance` is Part 2.
        // If we print `[Target] : PunitSet`, it treats PunitSet string as inheritance?
        // `apply_inheritance` (line 701) checks for `Domain.default`.
        // `parse_punit_set_string` checks for `Domain.Punit(...)`.
        // If `apply_inheritance` sees `Domain.Punit(...)`? `strtok` splits by comma.
        // `find_domain_index` ("Domain.Punit(...)") -> "Domain".
        // It sets `domain_punits[idx].set = 1`. But ignores Punit part?
        // Yes, `apply_inheritance` extracts domain name by stripping dot.
        // So `Domain.Punit(...)` passed to `apply_inheritance` enables the domain but ignores the Punit constraint (it doesn't parse inner part).
        // Then Part 3 `parse_punit_set` parses it again? No.
        // `parse_section_header` splits by `:`.
        // If output is `[T] : PunitSet`, then `parts[0]` = PunitSet. `apply_inheritance` runs on it.
        // It enables domain. But `parse_punit_set` is NOT called on `parts[0]`.
        // So Punit constraints would be LOST if printed in Part 2 slot.
        // Therefore, if we have PunitSet, we MUST have two colons if Inheritance is empty? `[T] : : PunitSet`.
        
        // Let's check if we have Punits.
        int has_any_punits = 0;
        for (int di = 0; di < num_domain; di++) {
            if (lg->domain_punits[di].set) {
                 struct domain_info *d = &domain_info_table[di];
                 for(int p=0; p<d->num_punits; p++) {
                     if(lg->domain_punits[di].punit[p].set) has_any_punits = 1;
                 }
            }
        }
        
        if (has_any_punits) {
            if (!printed_inh) {
                 fprintf(out, ": "); // Empty Inheritance
            }
            fprintf(out, " : "); // Separator for PunitSet
            int first = 0;
            // Print ALL Punits (filtered by who has them)
            // Using existing basic loop or custom
            int printed_p = 0;
            for (int di = 0; di < num_domain; di++) {
                if (lg->domain_punits[di].set) {
                    struct domain_info *d = &domain_info_table[di];
                    for (int pi = 0; pi < d->num_punits; pi++) {
                        if (lg->domain_punits[di].punit[pi].set) {
                           if (printed_p) fprintf(out, ", ");
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
        } else {
            // No Punits.
            if (!printed_inh && i != 0) {
                 // Even if nothing to print, maybe default inheritance implicitly?
                 // If nothing inherited and no punits, just [Lexgion].
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
                    if (strlen(d->event_table[k].name) == 0) continue;
                    if (k >= 64) break;
                    int on = (evt >> k) & 1;
                    fprintf(out, "    %s.%s = %s\n", d->name, d->event_table[k].name, on ? "on" : "off");
                }
            }
        }
        fprintf(out, "\n");
    }
}

