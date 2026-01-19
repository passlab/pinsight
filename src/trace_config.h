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
#include "bitset.h"

#define PINSIGHT_DEBUG "PINSIGHT_DEBUG"
#define PINSIGHT_DEBUG_DEFAULT 0

//DOMAINS: MPI, OpenMP, CUDA, OPENCL, HIP, PYTHON, ENERGY, BACKTRACE
#define MAX_NUM_DOMAINS   8
//We use the term punit (parallel unit) to denote the smallest schedulable and
//traceable execution entity—e.g., an MPI rank, an OpenMP thread, or a CUDA thread/warp.
//A punit is numbered starting from 0 to N-1, where N is the total number of punits
//PUNIT KINDS: process_rank, team, thread, device
#define MAX_NUM_PUNIT_KINDS 4
//MAX 63 events per domain (because we use a 64-bit BitSet to store event config, with bit 63 as global enable/disable flag
#define MAX_NUM_DOMAIN_EVENTS 63
//we do not consider subdomain in the implementation, e.g. for OpenMP, we have parallel, tasking, synchronization subdomain
//For MPI, we have p2p, collective communication, async communication. The reason we do not consider subdomain is to reduce
//the complexity of the implementation. But the data structure is designed to allow subdomain extension in

#define MAX_EVENT_ID           (MAX_NUM_DOMAIN_EVENTS - 1)  /* 62 */
#define DOMAIN_GLOBAL_BIT      (MAX_NUM_DOMAIN_EVENTS)      /* 63 */
#define EVENT_STATUS_NBITS     (DOMAIN_GLOBAL_BIT)          /* max index */

//Data structure for storing domain info, event info, punit info
typedef struct domain_info {
	char name[16];
	struct subdomain {
		char name[16];
		BitSet events; //The events that are categorized into this subdomain
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
	BitSet eventInstallStatus;  //For setting whether the event is enabled or not

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

/**
 * We use a single bit (ON/OFF) to enable/disable the tracing of an event. For each domain, we allow to have max
 * 63 events, with bit 63 as a global flag for enabling/disabling the domain
 */
typedef struct event_trace_config {
	unsigned long int status;
	unsigned long int toChange;
	unsigned long int newStatus;
} event_trace_config_t;

typedef struct rate_trace_config {
    unsigned int trace_starts_at;      //integer: the number of execution of the region before tracing starts.
    unsigned int initial_trace_count;  //integer: the number of traces to be collected after the trace starts the first time
    unsigned int max_num_traces;       //integer: total number of traces to be collected
    unsigned int tracing_rate;         //integer: the rate an execution is traced, e.g. 10 for 1 trace per 10 execution of the region.
} rate_trace_config_t;

/**
 * This is per domain-punit trace config, for each domain-punit, we can configure the event tracing (ON/OFF) and the lexgion rate tracing.
 * The configuration can be constrained by the specific punit specified as a range, e.g. from thread 0 to thread 3
 *
 * We use the term punit (parallel unit) to denote the smallest schedulable and traceable
 * execution entity—e.g., an MPI rank, an OpenMP thread, or a CUDA thread/warp.
 */
typedef struct trace_config {
	int punit; //which punit kind this config applies to, e.g. process_rank, team, thread, device
	BitSet punit_ids; //The specific punits that the trace config applies to.
	//Right now, we only support a single punit kind per config,
	//how to support multiple punit kinds per config is TBD?
	struct {
		int domain;
		int punit;
		BitSet punit_ids;
	} punit_cond[MAX_NUM_PUNIT_KINDS];
	int num_punit_conds;
	//punit range is to add more constrains on which punits the trace config should be applied to, e.g.
	//trace MPI_Send/Recv events only MPI process rank from 0 to 3, trace parallel_begin/end only for OpenMP thread 0 and MPI process rank 4
	event_trace_config_t event_config;
	rate_trace_config_t rate_config;

	struct trace_config * parent; //The pointer that forms the config tree/hierarchy according to the priority.
	struct trace_config * punit_kind_next; //The link list for the config of all the punit kinds of the domain
	struct trace_config * punit_next; //The link list for the config of the same punit group of the domain, e.g. for all threads config of OpenMP domain
} trace_config_t;

// --------------------------------------------------------
// Safe environment variable query functions
long env_get_long(const char* varname, long default_value);
unsigned long env_get_ulong(const char* varname, unsigned long default_value);

#endif // CONFIG_TRACE_CONFIG_H
