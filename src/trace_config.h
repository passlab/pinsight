#ifndef CONFIG_TRACE_CONFIG_H
#define CONFIG_TRACE_CONFIG_H
/**
 * Runtime configuration and reconfiguration of tracing is critical to truly enable dynamic,
 * region-specific and low-overhead tracing. To support this feature, the design needs to
 * consider 1) config/reconfig scope, i.e. what can/should be reconfigured,
 *          2) what configuration options can be used for each scope,
 *          3) when the reconfiguration should be applied,
 *          4) how the reconfiguration should be specified, and
 *          5) how to support efficient implementation
 *
 * For what can/should be configured, there are four scopes, from high level to low level,
 * that tracing configuration can be applied to:
 *   1. Domain scope, e.g. MPI, OMP, CUDA, Backtrace, and Energy can be reconfigured completely
 *   2. Event scope, i.e. specific event within a domain, e.g. OMP barrier, MPI_Bcast, CUDA Memcpy, etc can be reconfigured
 *   3. TPD scope, TPD stands for thread/process/device. For this scope, we can configure tracing for specific threads, processes or accelerator devices
 *   4. Lexgion scope, which is a specific code region identified via codeptr that can be add2lined to sourceFile:lineNumber.
 *      A lexgion can have multiple events from the same or different domains.
 *
 * For configuration options:
 * For the first three scopes, the option is either enabled or disabled. For the lexgion scope, the options are
 * disabled, counted tracing, and uncounted tracing. Counted tracing allows trace only specific amount of traces for a code
 * region, thus reducing the redundant tracing and overhead. Counted tracing requires internal book-keeping (for counting)
 * that involves array lookup, thus overhead is high compared with uncounted tracing.
 * But controlling the amount of tracing often reduces much more overhead than the overhead introduced by counting.
 * Uncounted tracing, which is by tracing all the events of the lexgion without internal book-keeping for lookup and counting,
 * is simple to implement and has low overhead of tracing.
 *
 * Thus for each scope, the options that are available are:
 *   1. Domain scope:  disabled and enabled
 *   2. Event scope:   disabled and enabled
 *   3. TPD scope:     Specified by numbers
 *   4. Lexgion scope: disabled, counted and uncounted
 *
 * For when to apply reconfiguration, considering the following points of execution of a program:
 *    1. When the program starts,
 *    2. pre-defined reconfiguration point, e.g. counted tracing will need to change the configuration
 *       when traces reach max count
 *    3. through signal handler, and
 *    4. debug breakpoint
 *
 * For how the configuration should be specified, the following two options can be used to provide configuration setting:
 *    1. via environment variable, and
 *    2. via config file.
 *
 * For implementation, we need to consider the following:
 *    1. Data structure for storing the configuration object,
 *    2. Functions for updating the configuration object, and
 *    3. Functions for apply the configuration.
 *    4. Knobs (functions) for initiate configuration/reconfiguration
 *    5. For the order of checking configuration, inside a tracing function, e.g.  an OMP callback,
 *       it should first check domain configuration, then TPD configuration, and the event configuration,
 *       and then lexgion configuration
 *    6. Tracing configuration takes effects on per process/thread-basis such that every thread process
 *       tracing control by itself. But configuration setting are applied globally.
 */
void pinsight_load_trace_config(char * filepath);

#include "bitset.h"

#define PINSIGHT_DEBUG "PINSIGHT_DEBUG"
#define PINSIGHT_DEBUG_DEFAULT 0

//DOMAINS: MPI, OpenMP, CUDA, OPENCL, HIP, PYTHON, ENERGY, BACKTRACE
#define MAX_NUM_DOMAINS   8
//We use the term punit (parallel unit) to denote the smallest schedulable and
//traceable execution entity—e.g., an MPI rank, an OpenMP thread, or a CUDA device.
//A punit is numbered starting from 0 to N-1, where N is the total number of punits
//PUNIT KINDS: process_rank, team, thread, device, number of punit kinds each domian can have max
#define MAX_NUM_PUNIT_KINDS 4
//MAX 64 events per domain because we use a 64-bit BitSet to store event config
#define MAX_NUM_DOMAIN_EVENTS 64
#define MAX_EVENT_ID           (MAX_NUM_DOMAIN_EVENTS - 1)  /* 63 */
//Each domain has its own multiple subdomains, e.g. OpenMP domain can have parallel, tasking, synchronization subdomain
//For MPI, we have p2p, collective communication, async communication.
//In the tracing, we do not differentiate events between subdomains to reduce the complexity and overhead of checking multiple subdomain configs.
//We however keep the event/subdomain info in the domain_info_table
#define MAX_NUM_LEXGIONS 512

/**
 * We use a single bit (ON/OFF) to enable/disable the tracing of an event. For each domain, we allow to have max
 * 63 events, with bit 63 as a global flag for enabling/disabling the domain
 */
/**
 * This is per domain-punit trace config, for each domain-punit, we can configure the event tracing (ON/OFF) and the lexgion rate tracing.
 * The configuration can be constrained by the specific punit specified as a range, e.g. from thread 0 to thread 3
 *
 * a trace config can be applied to a specified list of punit of a specific punit kind, e.g. OpenMP thread 0,1,2,3, then it can
 * be constrained by multiple punits of multiple punit kinds of other domains.
 *
 * trace config hierarchy/priority: global domain enable/disable ->punit enable/disable ->
 *                                  event enable/disable -> lexgion rate tracing
 * The global domain enable/disable config has the highest priority, if the domain is disabled, then no tracing is performed.
 * The global domain setting must be checked against the installation status of each domain/event when a config is read or reconfigured.
 *
 * We use the term punit (parallel unit) to denote the smallest schedulable and traceable
 * execution entity—e.g., an MPI rank, an OpenMP thread, or a CUDA thread/warp.

 * Consider three kinds of tracing options: 
 * 1) domain trace config where we only configure which events will be traced 
 *    with punit constraints, e.g. OpenMP thread 0,1,2,3, then it can
 *    be constrained by multiple punits of multiple punit kinds of other domains.
 *    With this, it is pure flat tracing of the selected events for the selected punits. The only way to 
 *    enable selective tracing is by using a timer, e.g. we only tracing for a certain period of time
 * 2) lexgion trace config where we configure the tracing rate for a specific code region, thus achieving
 *    selective tracing of the selected events for the selected punits. The event and punit constraints
 *    can be applied by either with the domain trace config or specified in the lexgion trace config.
 */


//The punit set for a domain
typedef struct domain_punit_set {
	int set; //The flag to indicate whether this domain's punit constraint is set
	struct {
		int set; //The flag to indicate whether this punit kind constraint is set
		BitSet punit_ids;
	} punit[MAX_NUM_PUNIT_KINDS];
} domain_punit_set_t;

typedef struct punit_trace_config {
	domain_punit_set_t domain_punits[MAX_NUM_DOMAINS];
	//domain/punit range for adding constrains on which punits the trace config should be applied to, e.g.
	//trace MPI_Send/Recv events only MPI process rank from 0 to 3, trace parallel_begin/end only for OpenMP thread 0 and MPI process rank 4
	unsigned long int events; //The event config for the this punit set 

	struct punit_trace_config * next;  //The link list pointer to the next punit_trace_config of the same domain
} punit_trace_config_t;

/**
 * A domain trace config includes a default trace config for the domain and 
 * a link list of punit-specific trace configs. punit-specific trace config specify
 * the events on/off applied to a specific punit set of the domain, constrained by the given punit sets of other domains if supplied.
*/
typedef struct domain_trace_config { //The trace config for a domain
	int set; //The global flag to indicate whether tracing is enabled for this domain
	unsigned long int events; //The default event config for the domain
	struct punit_trace_config * punit_trace_config; //The link list for punit_id-specific config of the domain.
} domain_trace_config_t;
extern domain_trace_config_t domain_trace_config[MAX_NUM_DOMAINS];

/**
 * A lexgion trace config includes the triple (trace_starts_at, max_num_traces, tracing_rate) for rate tracing, 
 * and the event on/off config for each domain constrained by the given punit sets of one or multiple domains. 
*/
typedef struct lexgion_trace_config {
	int domain_punit_set_set; //The flag to indicate whether the domain_punits is set
	domain_punit_set_t domain_punits[MAX_NUM_DOMAINS]; //The punit set for this trace config
    struct { //The event setting for each domain of this lexgion
		int set;
		unsigned long int events;
	} domain_events[MAX_NUM_DOMAINS];

	//rate trace config triple: trace_starts_at, max_num_traces, tracing_rate
	unsigned int trace_starts_at;      //integer: the number of execution of the region before tracing starts.
    unsigned int max_num_traces;       //integer: total number of traces to be collected
    unsigned int tracing_rate;         //integer: the rate an execution is traced, e.g. 10 for 1 trace per 10 execution of the region.
	void * codeptr;
} lexgion_trace_config_t;

extern lexgion_trace_config_t all_lexgion_trace_config[]; 
extern lexgion_trace_config_t *lexgion_trace_config_default; 
extern lexgion_trace_config_t *domain_lexgion_trace_config_default;
extern lexgion_trace_config_t *lexgion_trace_config;
extern int num_lexgion_trace_configs;

//Data structure for storing domain info, event info, punit info
typedef struct domain_info {
	char name[16];
	struct subdomain {
		char name[16];
		unsigned long int events; //The events that are categorized into this subdomain (a big map to specify which events)
	} subdomains[16]; //Max 16 subdomains
	int num_subdomains;

	struct event {
		int native_id; //The id that is mapped to the native ID of the event for each domain
		char name[64];
		int subdomain; //Events can be categorized into subdomains, such as MPI p2p, collective, sync, etc; or OpenMP parallel, task, target, etc
		void * callback;
        unsigned char valid;  /* 1 if this event_table slot is valid, 0 if unused (sparse IDs) */
	} event_table[MAX_NUM_DOMAIN_EVENTS]; //max 63 events (b.c. the BitSet we use for event config
    int num_events;        /* number of events declared (dense count) */
    int event_id_upper;    /* iteration upper bound: max_event_id + 1 */

	unsigned long int eventInstallStatus;  //For setting whether the event is enabled or not
	
	struct punit {
		char name[16];
		unsigned int low; //should be 0
		unsigned int high;
		union {
			int (*func0)(void); //function pointer to get the current punit id
			int (*func1)(void*); //function pointer to get the current punit id
		} punit_id_func;
		void * arg; //argument to be passed to the punit_id_func.func1
		int num_arg; //0 or 1 argument for the punit_id_func,
	} punits[4]; //max 4 punit kinds
	int num_punits;
} domain_info_t;
extern int num_domain;
extern struct domain_info domain_info_table[MAX_NUM_DOMAINS];

// --------------------------------------------------------
// Safe environment variable query functions
long env_get_long(const char* varname, long default_value);
unsigned long env_get_ulong(const char* varname, unsigned long default_value);

#endif // CONFIG_TRACE_CONFIG_H
