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
        fprintf(stderr, "PInSight: Could not open trace configuration file.\n");
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

// Example section: [Target : Inheritance : PunitSet]
// Target: Lexgion(...) or Domain.Kind(Range)
static int parse_section_header(char *line) {
    // Remove trailing brackets and whitespace
    char *end = line + strlen(line) - 1;
    while (end > line && (*end == ']' || isspace((unsigned char)*end))) {
        *end = '\0';
        end--;
    }
    char *content = line + 1;
    // Also skip leading spaces
    while (*content && isspace((unsigned char)*content)) content++;

    // Split by ':'
    char *parts[3] = {NULL, NULL, NULL};
    int part_count = 0;
    char *token = strtok(content, ":");
    while (token && part_count < 3) {
        parts[part_count++] = trim_whitespace(token);
        token = strtok(NULL, ":");
    }

    // Part 1: Target
    if (part_count < 1) return -1;
    char *target = parts[0];

    // Reset current context
    current_section_type = SECTION_NONE;
    current_domain_idx = -1;
    current_lexgion_config = NULL;
    current_punit_config = NULL; 

    // Check if it's a Lexgion
    // Check if it's Lexgion(0x...)
    if (strncmp(target, "Lexgion(", 8) == 0) {
        // Case 4: Lexgion(0x...)
        // Part 1: Address
        current_section_type = SECTION_LEXGION;
        char *ptr_start = strchr(target, '(');
        char *ptr_end = strchr(target, ')');
        if (ptr_start && ptr_end) {
            *ptr_end = '\0';
            // Parse hex or decimal address
            uint64_t addr = strtoull(ptr_start + 1, NULL, 0);
            void *codeptr = (void*)(uintptr_t)addr;
            
            // Create or get config (initialized with defaults)
            current_lexgion_config = get_or_create_lexgion_config(codeptr);
            
            // Part 2: Inheritance (Optional)
            if (current_lexgion_config && part_count >= 2 && parts[1]) {
                 if (apply_inheritance(current_lexgion_config, parts[1]) != 0) {
                     return -1; // Error in inheritance (e.g. domain not configured)
                 }
            }
            
            // Part 3: Punit Set (Optional)
            if (current_lexgion_config && part_count >= 3 && parts[2]) {
                parse_punit_set_string(parts[2], current_lexgion_config->domain_punits);
            }
        }
    } 
    // Check if it's Lexgion.default
    else if (strcmp(target, "Lexgion.default") == 0) {
        // Case 2: Lexgion.default
        current_section_type = SECTION_LEXGION_DEFAULT;
        current_lexgion_config = &lexgion_trace_config[0];
        num_lexgion_trace_configs++; 
        
        // Part 2: Inheritance (Optional)
        if (part_count >= 2 && parts[1]) {
             if (apply_inheritance(current_lexgion_config, parts[1]) != 0) {
                 return -1; 
             }
        }
        
        // Part 3: Not expected for Lexgion.default? User said "inheritence from 0 to multiple domain.default separated by ,,"
        // Did not explicitly mention PunitSet for Lexgion.default.
        // Assuming not used.
    }
    // Check if it's Domain.default
    else if (strstr(target, ".default")) {
        // Case 1: Domain.default
        current_section_type = SECTION_DOMAIN_DEFAULT;
        
        // Enforce 1 part
        if (part_count > 1) {
            fprintf(stderr, "PInSight Error: [Domain.default] header cannot have inheritance or punit set.\n");
            return -1;
        }

        char *dot = strchr(target, '.');
        if (dot) {
            *dot = '\0';
            current_domain_idx = find_domain_index(target);
            if (current_domain_idx >= 0) {
                 // Domain configured (valid index found)
            }
        }
    }
    // Assume Domain.punit specification
    else {
        // Case 3: Domain.punit
        current_section_type = SECTION_DOMAIN_PUNIT;
        
        // Parse Part 1 (Primary Punit Constraint) to find domain and create config
        char temp_target[MAX_LINE_LENGTH];
        strncpy(temp_target, target, sizeof(temp_target));
        char *dot = strchr(temp_target, '.');
        
        int dom_idx = -1;
        if (dot) {
            *dot = '\0';
            dom_idx = find_domain_index(temp_target);
        }
        
        if (dom_idx >= 0) {
             current_domain_idx = dom_idx;
             // Create a punit_trace_config
             punit_trace_config_t *new_config = malloc(sizeof(punit_trace_config_t));
             memset(new_config, 0, sizeof(punit_trace_config_t));
             
             // Link (Append to end)
             new_config->next = NULL;
             if (domain_trace_config[dom_idx].punit_trace_config == NULL) {
                 domain_trace_config[dom_idx].punit_trace_config = new_config;
             } else {
                 punit_trace_config_t *curr = domain_trace_config[dom_idx].punit_trace_config;
                 while(curr->next) curr = curr->next;
                 curr->next = new_config;
             }
             
             // Parse Part 1 into domain_punits array
             if (parse_punit_set_string(target, new_config->domain_punits) != 0) {
                 fprintf(stderr, "PInSight Error: Failed to parse punit set string: %s\n", target);
             }
             
             // Part 2: Inheritance (Optional)
             // "inheritence of default of the same domain as the second part"
             if (part_count >= 2 && parts[1]) {
                 char *inh_str = parts[1];
                 char *d_dot = strstr(inh_str, ".default");
                 if (d_dot) {
                     *d_dot = '\0'; // Isolate Domain name
                     int parent_idx = find_domain_index(inh_str);
                     if (parent_idx >= 0) {
                         // Copy events from parent
                         new_config->events = domain_trace_config[parent_idx].events;
                     }
                 }
             }
             
             // Part 3: Punit Set (Additional constraints) (Optional)
             if (part_count >= 3 && parts[2]) {
                 parse_punit_set_string(parts[2], new_config->domain_punits);
             }
             
             current_punit_config = new_config;
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
static void print_punit_set(FILE *out, domain_punit_set_t *set_array, int *first_printed) {
    for (int di = 0; di < num_domain; di++) {
        if (set_array[di].set) {
            struct domain_info *target_domain = &domain_info_table[di];
            for (int pi = 0; pi < target_domain->num_punits; pi++) {
                if (set_array[di].punit[pi].set) {
                    if (*first_printed == 0) {
                        // First item printed in this context
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
            // Skip invalid or empty events
            if (strlen(d->event_table[k].name) == 0) continue;
            
            // Check if k is within range of what fits in unsigned long (64)
            if (k >= 64) break; 
            
            int on = (current_events >> k) & 1;
            fprintf(out, "    %s = %s\n", d->event_table[k].name, on ? "on" : "off");
        }
        fprintf(out, "\n");

        punit_trace_config_t *curr = domain_trace_config[i].punit_trace_config;
        while (curr) {
            fprintf(out, "[");
            int first = 0; // Not printed yet
            print_punit_set(out, curr->domain_punits, &first);
            
            fprintf(out, ": %s.default]\n", d->name);
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
            /* Print the default lexgion section */
            fprintf(out, "[Lexgion.default");
        } else {
            fprintf(out, "[Lexgion(%p)", lg->codeptr);
        }

        int first_inh = 1;
        for (int di = 0; di < num_domain; di++) {
            if (lg->domain_punits[di].set) {
                if (first_inh) { fprintf(out, ": "); first_inh = 0; }
                else { fprintf(out, ", "); }
                fprintf(out, "%s.default", domain_info_table[di].name);
            }
        }
        
        // Print Punit Set (Part 3)
        /* For the default lexgion entry we do not print a punit-set (part 3).
         * For non-default lexgions, print the punit set after a colon. */
        if (i != 0) {
            int dummy_first = 0;
            fprintf(out, " : ");
            print_punit_set(out, lg->domain_punits, &dummy_first);
        }
        
        fprintf(out, "]\n");
        
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

