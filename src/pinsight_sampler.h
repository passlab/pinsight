/*
 * pinsight_sampler.h — the generic sampler framework (S1).
 *
 * The SAMPLE path: PInsight's second collection instrument, independent of
 * the trace-mode ladder (hybrid design REV 10; sampler_impl_plan.md).
 * Domains register thread-agnostic PROBES; the framework runs one shared
 * sampler thread by default, plus a dedicated thread per probe whose domain
 * sets `sampler_thread = own` in its [Domain.sampling] section. Threads are
 * created LAZILY (first time a probe's enable condition becomes true) and
 * PARK on a condvar when nothing they schedule is enabled — a quiescent
 * config costs zero threads and zero wakeups.
 *
 * Enable condition per probe:
 *   interval_us > 0  &&  (infra_min_trace_mode == NONE ||
 *                         domain mode >= infra_min_trace_mode)
 *   && this process passed the probe's process_fraction draw.
 *
 * The control thread is the conductor: it calls pinsight_sampler_kick()
 * after every config reload / mode change / window transition so parked or
 * not-yet-created threads re-evaluate. Probe callbacks run on a framework
 * thread (pulled probes); the future app-thread-signal probes (G1/G4) will
 * use the framework thread as their PACER only.
 */
#ifndef PINSIGHT_SAMPLER_H
#define PINSIGHT_SAMPLER_H

#include "trace_config.h"
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

struct pinsight_sampler_sched;

typedef struct pinsight_probe {
  /* ---- caller-filled ---- */
  int domain_index;   /* rate/config read from domain_sampling_config[] */
  const char *name;   /* "python_stack", later "gpu_counters", ... */
  void (*probe)(void *arg); /* collect + emit one tick (pulled execution) */
  void *arg;
  domain_trace_mode_t infra_min_trace_mode; /* INFRASTRUCTURE dependency,
      NOT a policy coupling: sampling's only switch is interval_us (the
      two-instrument rule — tracing never gates sampling). This field says
      "my data source lives inside event-path tool infrastructure that
      must at least EXIST at this mode level":
        NONE (0)  = fully independent — pure sampler with trace_mode=OFF
                    works (Python stack probe: the interpreter IS the app;
                    future GPU-counter/MPI_T probes: NVML/MPI_T interfaces
                    are independent of our CUPTI/PMPI machinery).
        STANDBY   = the domain's tool must be registered/alive even though
                    it emits nothing (future G1 OMPT-state probe:
                    ompt_get_state() needs the OMPT tool, whose
                    registration is a one-shot runtime-startup decision
                    that trace_mode=OFF forgoes permanently).
      Unsatisfied → probe stays parked with one warning; a later mode
      change un-parks it via pinsight_sampler_kick(). */

  /* ---- framework-owned (zeroed by pinsight_probe_register) ---- */
  struct pinsight_sampler_sched *sched;
  struct timespec next_deadline; /* CLOCK_MONOTONIC; zero = not yet armed */
  int deadline_armed;
  int in_call;            /* probe callback currently executing */
  int fraction_excluded;  /* lost the process_fraction draw (latched) */
  int fraction_drawn;
  int latched_own;        /* sampler_thread value latched at registration */
  int warned_latched;     /* reload tried to change a latched key */
  int warned_min_mode;
  /* per-probe cost stats (written only by the sampling thread; read at
   * unregister) — overhead-self-estimation O1's first counters */
  unsigned long ticks;
  unsigned long missed_ticks; /* deadline overruns (Tachyon-stats analog) */
  unsigned long long cost_ns_total;
  unsigned long long cost_ns_min;
  unsigned long long cost_ns_max;
} pinsight_probe_t;

/* Register a probe. Bookkeeping only — no thread is created until the
 * probe's enable condition first becomes true (lazy creation). The probe
 * struct must outlive the registration. Returns 0, or -1 if the registry
 * is full. */
int pinsight_probe_register(pinsight_probe_t *p);

/* Unregister: waits for any in-flight callback of this probe to return;
 * joins and destroys a scheduling thread that becomes empty; prints the
 * probe's cost/tick summary to stderr. */
void pinsight_probe_unregister(pinsight_probe_t *p);

/* Conductor entry point (control thread after reload/mode/window changes;
 * also callable from domain glue such as set_trace_mode): re-evaluate all
 * probes — lazily spawn threads for newly-enabled probes, wake parked
 * threads, pick up hot-reloaded intervals. Safe from any thread. */
void pinsight_sampler_kick(void);

#ifdef __cplusplus
}
#endif

#endif /* PINSIGHT_SAMPLER_H */
