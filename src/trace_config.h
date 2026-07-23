#ifndef CONFIG_TRACE_CONFIG_H
#define CONFIG_TRACE_CONFIG_H
/**
 * Runtime configuration and reconfiguration of tracing is critical to truly
 * enable dynamic, region-specific and low-overhead tracing. To support this
 * feature, the design needs to consider 1) config/reconfig scope, i.e. what
 * can/should be reconfigured, 2) what configuration options can be used for
 * each scope, 3) when the reconfiguration should be applied, 4) how the
 * reconfiguration should be specified, and 5) how to support efficient
 * implementation
 *
 * For what can/should be configured, there are four scopes, from high level to
 * low level, that tracing configuration can be applied to:
 *   1. Domain scope, e.g. MPI, OMP, CUDA, Backtrace, and Energy can be
 * reconfigured completely
 *   2. Event scope, i.e. specific event within a domain, e.g. OMP barrier,
 * MPI_Bcast, CUDA Memcpy, etc can be reconfigured
 *   3. TPD scope, TPD stands for thread/process/device. For this scope, we can
 * configure tracing for specific threads, processes or accelerator devices
 *   4. Lexgion scope, which is a specific code region identified via codeptr
 * that can be add2lined to sourceFile:lineNumber. A lexgion can have multiple
 * events from the same or different domains.
 *
 * For configuration options:
 * For the first three scopes, the option is either enabled or disabled. For the
 * lexgion scope, the options are disabled, counted tracing, and uncounted
 * tracing. Counted tracing allows trace only specific amount of traces for a
 * code region, thus reducing the redundant tracing and overhead. Counted
 * tracing requires internal book-keeping (for counting) that involves array
 * lookup, thus overhead is high compared with uncounted tracing. But
 * controlling the amount of tracing often reduces much more overhead than the
 * overhead introduced by counting. Uncounted tracing, which is by tracing all
 * the events of the lexgion without internal book-keeping for lookup and
 * counting, is simple to implement and has low overhead of tracing.
 *
 * Thus for each scope, the options that are available are:
 *   1. Domain scope:  disabled and enabled
 *   2. Event scope:   disabled and enabled
 *   3. TPD scope:     Specified by numbers
 *   4. Lexgion scope: disabled, counted and uncounted
 *
 * For when to apply reconfiguration, considering the following points of
 * execution of a program:
 *    1. When the program starts,
 *    2. pre-defined reconfiguration point, e.g. counted tracing will need to
 * change the configuration when traces reach max count
 *    3. through signal handler, and
 *    4. debug breakpoint
 *
 * For how the configuration should be specified, the following two options can
 * be used to provide configuration setting:
 *    1. via environment variable, and
 *    2. via config file.
 *
 * For implementation, we need to consider the following:
 *    1. Data structure for storing the configuration object,
 *    2. Functions for updating the configuration object, and
 *    3. Functions for apply the configuration.
 *    4. Knobs (functions) for initiate configuration/reconfiguration
 *    5. For the order of checking configuration, inside a tracing function,
 * e.g.  an OMP callback, it should first check domain configuration, then TPD
 * configuration, and the event configuration, and then lexgion configuration
 *    6. Tracing configuration takes effects on per process/thread-basis such
 * that every thread process tracing control by itself. But configuration
 * setting are applied globally.
 */

#include "bitset.h"
#include <signal.h>
#include <stdio.h>

#define PINSIGHT_DEBUG "PINSIGHT_DEBUG"
#define PINSIGHT_DEBUG_DEFAULT 0

// DOMAINS: MPI, OpenMP, CUDA, OPENCL, HIP, PYTHON, ENERGY, BACKTRACE
#define MAX_NUM_DOMAINS 8
// We use the term punit (parallel unit) to denote the smallest schedulable and
// traceable execution entity—e.g., an MPI rank, an OpenMP thread, or a CUDA
// device. A punit is numbered starting from 0 to N-1, where N is the total
// number of punits PUNIT KINDS: process_rank, team, thread, device, number of
// punit kinds each domian can have max
#define MAX_NUM_PUNIT_KINDS 4
// MAX 64 events per domain because we use a 64-bit BitSet to store event config
#define MAX_NUM_DOMAIN_EVENTS 64
#define MAX_EVENT_ID (MAX_NUM_DOMAIN_EVENTS - 1) /* 63 */
// Max node-policy keys a domain can declare (e.g. HIP/CUDA "device_activity").
#define MAX_NUM_NODEPOLICY_KEYS 4
// Each domain has its own multiple subdomains, e.g. OpenMP domain can have
// parallel, tasking, synchronization subdomain For MPI, we have p2p, collective
// communication, async communication. In the tracing, we do not differentiate
// events between subdomains to reduce the complexity and overhead of checking
// multiple subdomain configs. We however keep the event/subdomain info in the
// domain_info_table
#define MAX_NUM_LEXGIONS 512

/**
 * We use a single bit (ON/OFF) to enable/disable the tracing of an event. For
 * each domain, we allow to have max 63 events, with bit 63 as a global flag for
 * enabling/disabling the domain
 */
/**
 * This is per domain-punit trace config, for each domain-punit, we can
 configure the event tracing (ON/OFF) and the lexgion rate tracing.
 * The configuration can be constrained by the specific punit specified as a
 range, e.g. from thread 0 to thread 3
 *
 * a trace config can be applied to a specified list of punit of a specific
 punit kind, e.g. OpenMP thread 0,1,2,3, then it can
 * be constrained by multiple punits of multiple punit kinds of other domains.
 *
 * trace config hierarchy/priority: global domain enable/disable ->punit
 enable/disable ->
 *                                  event enable/disable -> lexgion rate tracing
 * The global domain enable/disable config has the highest priority, if the
 domain is disabled, then no tracing is performed.
 * The global domain setting must be checked against the installation status of
 each domain/event when a config is read or reconfigured.
 *
 * We use the term punit (parallel unit) to denote the smallest schedulable and
 traceable
 * execution entity—e.g., an MPI rank, an OpenMP thread, or a CUDA thread/warp.

 * Consider three kinds of tracing options:
 * 1) domain trace config where we only configure which events will be traced
 *    with punit constraints, e.g. OpenMP thread 0,1,2,3, then it can
 *    be constrained by multiple punits of multiple punit kinds of other
 domains.
 *    With this, it is pure flat tracing of the selected events for the selected
 punits. The only way to
 *    enable selective tracing is by using a timer, e.g. we only tracing for a
 certain period of time
 * 2) lexgion trace config where we configure the tracing rate for a specific
 code region, thus achieving
 *    selective tracing of the selected events for the selected punits. The
 event and punit constraints
 *    can be applied by either with the domain trace config or specified in the
 lexgion trace config.
 */

// The punit set for a domain
typedef struct domain_punit_set {
  int set; // The flag to indicate whether this domain's punit constraint is set
  struct {
    int set; // The flag to indicate whether this punit kind constraint is set
    BitSet punit_ids;
  } punit[MAX_NUM_PUNIT_KINDS];
} domain_punit_set_t;

/**
 * The punit trace config that specifies the events on/off for the domain,
 * constrained by the given punit sets of all domains if supplied.
 */
typedef struct punit_trace_config {
  domain_punit_set_t domain_punits[MAX_NUM_DOMAINS];
  // domain/punit range for adding constrains on which punits the trace config
  // should be applied to, e.g. trace MPI_Send/Recv events only MPI process rank
  // from 0 to 3, trace parallel_begin/end only for OpenMP thread 0 and MPI
  // process rank 4
  unsigned long int events; // The event config for the this punit set

  struct punit_trace_config *next; // The link list pointer to the next
                                   // punit_trace_config of the same domain
} punit_trace_config_t;

/**
 * Domain operating modes for fine-grained overhead control.
 *
 * OFF:        Permanent teardown — callbacks deregistered/finalized,
 *             zero per-event overhead, irreversible.
 * STANDBY:    Callback-ready but immediate return — near-zero overhead,
 *             reversible. Tool infrastructure stays alive.
 * MONITORING: LRU lookup + count only — no config resolution, no LTTng.
 * TRACING:    Full tracing with LTTng output (default).
 */
typedef enum {
  PINSIGHT_DOMAIN_NONE = 0, /* No mode specified (only for window_end_action) */
  PINSIGHT_DOMAIN_OFF = 1,  /* Permanent teardown */
  PINSIGHT_DOMAIN_STANDBY = 2,    /* Callback-ready, immediate return */
  PINSIGHT_DOMAIN_MONITORING = 3, /* LRU lookup + count, no LTTng */
  PINSIGHT_DOMAIN_TRACING = 4,    /* Full tracing (default) */
} pinsight_domain_mode_t;

/* Node-policy — WHICH ranks perform a per-process capability measurement
 * (HIP/CUDA GPU-activity pool, energy). Resolved once at startup (per-run).
 * See doc/node_singleton_measurement_design.md. */
typedef enum {
  PINSIGHT_NODEPOLICY_OFF = 0,         /* skip everywhere                          */
  PINSIGHT_NODEPOLICY_ON,              /* every rank measures/collects             */
  PINSIGHT_NODEPOLICY_ANYONE_PER_NODE, /* 1 arbitrary rank/node (lock-file)        */
  PINSIGHT_NODEPOLICY_LEADER_PER_NODE, /* 1 deterministic leader rank/node         */
  PINSIGHT_NODEPOLICY_ROTATE_PER_NODE, /* 1 rotating collector/node (Phase 3; HIP/CUDA) */
} pinsight_nodepolicy_t;

/* Runtime value = policy + optional param (rotate period ms; 0 unless ROTATE). */
typedef struct {
  pinsight_nodepolicy_t policy;
  int param;
} pinsight_nodepolicy_val_t;

/* True if domain is active (MONITORING or TRACING — does lexgion work) */
#define PINSIGHT_DOMAIN_ACTIVE(mode) ((mode) >= PINSIGHT_DOMAIN_MONITORING)
/* True if domain should emit LTTng tracepoints */
#define PINSIGHT_SHOULD_TRACE(domain)                                          \
  (domain_default_trace_config[domain].mode == PINSIGHT_DOMAIN_TRACING)
/* True if tool is alive (STANDBY, MONITORING, or TRACING — can be activated) */
#define PINSIGHT_DOMAIN_ALIVE(mode) ((mode) >= PINSIGHT_DOMAIN_STANDBY)

/* Convert mode enum to string — eliminates scattered ternary chains */
static inline const char *pinsight_mode_str(pinsight_domain_mode_t mode) {
  switch (mode) {
  case PINSIGHT_DOMAIN_OFF:
    return "OFF";
  case PINSIGHT_DOMAIN_STANDBY:
    return "STANDBY";
  case PINSIGHT_DOMAIN_MONITORING:
    return "MONITORING";
  case PINSIGHT_DOMAIN_TRACING:
    return "TRACING";
  default:
    return "NONE";
  }
}

/**
 * The default domain trace config that specifies the events on/off for the
 * domain
 */
typedef struct domain_trace_config { // The trace config for a domain
  volatile pinsight_domain_mode_t
      mode; // Domain operating mode (OFF/STANDBY/MONITORING/TRACING)
  volatile pinsight_domain_mode_t
      last_mode; // Previous mode (for transition detection, zero-init = NONE)
  volatile unsigned long int
      events; // The default event config for the domain, initially copied from
              // domain_info->events
  volatile int mode_change_fired; // Once-per-domain latch: set when end_action
                                  // fires, reset on config reload
  // Parsed node-policy values, indexed the same as domain_info.nodepolicy_keys[].
  // Set from [Domain.default]; defaults copied from the DSL-declared defaults.
  volatile pinsight_nodepolicy_val_t nodepolicy[MAX_NUM_NODEPOLICY_KEYS];
} domain_trace_config_t;
extern domain_trace_config_t domain_default_trace_config[MAX_NUM_DOMAINS];
extern punit_trace_config_t *domain_punit_trace_config[MAX_NUM_DOMAINS];

/* Count-trigger policy that ends a TRACING window via max_num_traces.
 *   first  = the first region to reach its cap fires (default; today's behavior).
 *   all    = fire when every region THIS THREAD has seen has capped (per-thread;
 *            the first thread to satisfy it trips the global latch).
 *   anchor = reserved, NOT yet implemented (a user-named region caps). The parser
 *            rejects this keyword for now. Enum so adding it later is additive. */
typedef enum window_end_trigger {
  TRIGGER_FIRST  = 0, /* default (zero-initialized) */
  TRIGGER_ALL    = 1,
  TRIGGER_ANCHOR = 2, /* reserved */
} window_end_trigger_t;

/**
 * Unified window_end_action action: handles both regular mode switching
 * (e.g. MONITORING, OpenMP:OFF) and INTROSPECT with timeout/script.
 *
 * For non-INTROSPECT: mode[] specifies per-domain target modes, introspect ==
 * 0. For INTROSPECT:     introspect == 1, introspect_pause_duration/introspect_script
 * set, mode[] specifies per-domain resume modes after introspection.
 */
typedef struct window_end_action {
  pinsight_domain_mode_t mode[MAX_NUM_DOMAINS]; // target mode per domain
                                                // (NONE = no change)
  int introspect;              // 1 = introspect before switching modes
  int introspect_pause_duration;      // >0: pause N seconds, 0: no pause, <0: wait
                               // indefinitely for SIGUSR1
  char introspect_script[256]; // script to invoke ("-" or "" = none)
  volatile int fired;          // latch: set by first lexgion to fire,
                               // prevents duplicate triggers in same cycle
  volatile unsigned int generation; // incremented each cyclic INTROSPECT cycle;
                                    // lexgions compare against their local
                                    // introspect_gen to auto-reset trace_counter
  window_end_trigger_t trigger;     // count-policy for the count trigger
                                    // (default TRIGGER_FIRST = 0)
  int window_timeout_sec;           // >0: wall-clock deadline (sec) ending the
                                    // TRACING window; 0/absent = disabled (default)
} window_end_action_t;

/**
 * A lexgion trace config includes the triple (trace_starts_at, max_num_traces,
 * tracing_rate) for rate-limit tracing, and the event on/off config for each
 * domain constrained by the given punit sets of one or multiple domains.
 */
typedef struct lexgion_trace_config {
  int domain_punit_set_set; // The flag to indicate whether the domain_punits is
                            // set
  domain_punit_set_t
      domain_punits[MAX_NUM_DOMAINS]; // The punit set for this trace config
  struct { // The event setting for each domain of this lexgion
    int set;
    unsigned long int events;
  } domain_events[MAX_NUM_DOMAINS];

  // rate trace config triple: trace_starts_at, max_num_traces, tracing_rate
  unsigned int trace_starts_at; // integer: the number of execution of the
                                // region before tracing starts.
  unsigned int max_num_traces;  // integer: total number of traces to be
                                // collected
  unsigned int tracing_rate; // integer: the rate an execution is traced, e.g.
                             // 10 for 1 trace per 10 execution of the region.
  void *codeptr;
  int removed; // The flag to indicate whether this lexgion(address) trace
               // config is removed.
               //  When it is removed , the object may still exist in the array
               //  but should not be used.

  /* Named lexgion fields. Empty name ("") means address-based config.
   * Named configs have codeptr==NULL and name[0]!='\0'. domain_index==-1
   * means match any domain; otherwise the config only matches that domain. */
  char name[128];         /* co_qualname, kernel name, MPI func, or "" */
  char filename_hint[64]; /* optional: source file basename for disambiguation */
  int domain_index;       /* -1 = any domain; else must match lexgion class */

  // Auto-trigger: switch domain modes when max_num_traces is reached
  window_end_action_t end_action;
} lexgion_trace_config_t;

// Default rate trace config: trace every execution,
// no limit on the number of traces, start from the first execution
#define DEFAULT_TRACE_START 0
#define DEFAULT_TRACE_MAX -1
#define DEFAULT_TRACE_RATE 1

extern lexgion_trace_config_t all_lexgion_trace_config[];
extern lexgion_trace_config_t *lexgion_default_trace_config;
extern lexgion_trace_config_t *lexgion_domain_default_trace_config;
extern lexgion_trace_config_t *lexgion_trace_config;
extern int num_lexgion_trace_configs;
extern unsigned int
    trace_config_change_counter; /* bumped on every reconfig to invalidate
                                cached trace_config in lexgions */
extern void pinsight_load_trace_config(char *filepath);
void parse_trace_config_file(char *filename);
extern int find_domain_index(const char *name);

// Unified parser for window_end_action values:
// "MONITORING", "OpenMP:OFF,MPI:MONITORING",
// "INTROSPECT:60:script.sh[:TRACING]"
extern int parse_window_end_action(const char *val, window_end_action_t *out);

// Check whether the current execution punit id's match the domain_punit_set
// constraints
extern int domain_punit_set_match(domain_punit_set_t *domain_punit_set);

// Data structure for storing domain info, event info, punit info
typedef struct domain_info {
  char name[16];
  struct subdomain {
    char name[16];
    unsigned long int events; // The events that are categorized into this
                              // subdomain (a big map to specify which events)
  } subdomains[16];           // Max 16 subdomains
  int num_subdomains;

  struct event {
    int native_id; // The id that is mapped to the native ID of the event for
                   // each domain
    char name[64];
    int subdomain; // Events can be categorized into subdomains, such as MPI
                   // p2p, collective, sync, etc; or OpenMP parallel, task,
                   // target, etc
    void *callback;
    unsigned char valid; /* 1 if this event_table slot is valid, 0 if unused
                            (sparse IDs) */
  } event_table[MAX_NUM_DOMAIN_EVENTS]; // max 63 events (b.c. the BitSet we use
                                        // for event config
  int num_events;     /* number of events declared (dense count) */
  int event_id_upper; /* iteration upper bound: max_event_id + 1 */

  unsigned long int
      eventInstallStatus; // For setting whether the event is enabled or not

  pinsight_domain_mode_t starting_mode;
  struct punit {
    char name[16];
    unsigned int low; // should be 0
    unsigned int high;
    union {
      int (*func0)(void);   // function pointer to get the current punit id
      int (*func1)(void *); // function pointer to get the current punit id
    } punit_id_func;
    void *arg;   // argument to be passed to the punit_id_func.func1
    int num_arg; // 0 or 1 argument for the punit_id_func,
  } punits[MAX_NUM_PUNIT_KINDS]; // max 4 punit kinds
  int num_punits;
  // Node-policy keys this domain declares via TRACE_NODEPOLICY (e.g. HIP/CUDA
  // "device_activity"). Name + DSL-declared default; the runtime value lives in
  // domain_trace_config_t.nodepolicy[] at the same index.
  struct nodepolicy_key {
    char name[32];
    pinsight_nodepolicy_t dflt;
  } nodepolicy_keys[MAX_NUM_NODEPOLICY_KEYS];
  int num_nodepolicy_keys;
} domain_info_t;
extern int num_domain;
extern struct domain_info domain_info_table[MAX_NUM_DOMAINS];

// Serialization functions
extern void dsl_print_domain_info(struct domain_info *d);
extern void print_domain_info(FILE *out);
extern void print_domain_trace_config(FILE *out);
extern void print_lexgion_trace_config(FILE *out);
extern void pinsight_load_trace_config(char *filepath);
extern void pinsight_invalidate_config_mtime(void);

// ---- Node-policy (device_activity / energy measure) ---------------------
// Parse "off|on|anyone_per_node|leader_per_node|rotate_per_node[:<ms>]"
// (case-insensitive). Unknown/NULL -> {dflt, 0}. rotate suffix -> {ROTATE, ms}.
extern pinsight_nodepolicy_val_t
pinsight_parse_nodepolicy(const char *val, pinsight_nodepolicy_t dflt);
extern const char *pinsight_nodepolicy_str(pinsight_nodepolicy_t p);
// Index of nodepolicy key <key> in domain <domain_index>, or -1. Parser/init use.
extern int pinsight_get_nodepolicy_index(int domain_index, const char *key);
// Does THIS process perform measurement <lockname> under <policy>? 1=yes, 0=skip.
// Handles OFF/ON/ANYONE/LEADER (ROTATE falls back to LEADER in Phase 1). Cached
// per <lockname> (incl. any held flock fd).
extern int pinsight_node_role(const char *lockname, pinsight_nodepolicy_t policy);
// Local rank within node, published by the MPI wrapper (-1 until set / no MPI).
extern int pinsight_mpi_local_rank;
// Energy node-singleton policy (default ON). Set by env PINSIGHT_MEASURE_ENERGY
// or [Energy] measure; read by energy.c. Defined here (not energy.c) so the
// standalone config-parser test links without energy.c. See energy.h.
extern pinsight_nodepolicy_t energy_measure_policy;

// --------------------------------------------------------
// Safe environment variable query functions
extern long env_get_long(const char *varname, long default_value);
unsigned long env_get_ulong(const char *varname, unsigned long default_value);

#endif // CONFIG_TRACE_CONFIG_H
