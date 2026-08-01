//
// PInsight energy/power — coordinator.
//
// Owns the dedicated energy_pinsight_lttng_ust provider and orchestrates the
// pluggable backends (see energy_backend.h). At init it applies the activation
// policy (NODE supersedes; at most one CPU; all GPU); at read it lets every
// active backend append its readings; at the enter/exit snapshots it emits the
// two energy sequences.
//
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "energy.h"
#include "energy_backend.h"

/* energy_measure_policy is DEFINED in trace_config.c (always linked, incl. the
 * standalone config-parser test which does not link energy.c); declared in
 * trace_config.h. Resolved at first arm: does THIS proc measure? */
static int energy_measure_active = 0;

/* ---- Armed-span state (2026-07-31 amendment, energy_power_implementation_plan.md)
 * A span is one armed interval bracketed by energy_enter/energy_exit sharing
 * seq = the span ordinal (0,1,2,...). Disarm ALWAYS closes the open span with a
 * final counter read — never a bare stop. Writers: the library constructor
 * (main thread, before the control thread exists) and the control thread
 * (reload transitions + shutdown) — never concurrent, plain ints suffice. */
static int energy_armed = 0;         /* inside an open span? */
static int energy_ever_armed = 0;    /* role + variant latched, backends inited */
static uint64_t energy_span_seq = 0; /* ordinal of the open (or next) span */
static pinsight_nodepolicy_t energy_latched_policy; /* variant at first arm */

/* The energy provider is defined in exactly this translation unit. */
#define LTTNG_UST_TRACEPOINT_CREATE_PROBES
#define LTTNG_UST_TRACEPOINT_DEFINE
#include "energy_lttng_ust_tracepoint.h"

/* All compiled-in backends, in activation-preference order. */
static const pinsight_energy_backend_t *const all_backends[] = {
#ifdef PINSIGHT_ENERGY_VARIORUM
    &pinsight_energy_backend_variorum, /* NODE — tried first, supersedes if active */
#endif
    &pinsight_energy_backend_powercap, /* CPU */
    &pinsight_energy_backend_amd_energy, /* CPU */
#ifdef PINSIGHT_ENERGY_AMD_GPU
    &pinsight_energy_backend_amd_smi, /* GPU */
#endif
};
#define NUM_BACKENDS ((int)(sizeof(all_backends) / sizeof(all_backends[0])))

static const pinsight_energy_backend_t *active[NUM_BACKENDS];
static int num_active = 0;

/* Effective measure policy: env PINSIGHT_MEASURE_ENERGY overrides the
 * [Energy] measure config value at EVERY evaluation (startup and reload). */
static pinsight_nodepolicy_t energy_resolved_policy(void) {
  const char *menv = getenv("PINSIGHT_MEASURE_ENERGY");
  if (menv && *menv)
    return pinsight_parse_nodepolicy(menv, energy_measure_policy).policy;
  return energy_measure_policy;
}

/* Backend discovery/init. Runs once, at first arm, and only on the measuring
 * process (keeps AMD-SMI init/teardown to one rank under *_per_node policies).
 * Backends stay initialized across disarm — re-arm skips this entirely. */
static void backends_init_once(void) {
  static int backends_inited = 0;
  if (backends_inited)
    return;
  backends_inited = 1;
  num_active = 0;

  int node_active = 0, cpu_active = 0;

  /* Pass 1: NODE backends (e.g. Variorum) — first readable supersedes the rest. */
  for (int i = 0; i < NUM_BACKENDS; i++) {
    if (all_backends[i]->kind != ENERGY_KIND_NODE)
      continue;
    if (!node_active && all_backends[i]->init() > 0) {
      active[num_active++] = all_backends[i];
      node_active = 1;
    }
  }

  /* If a NODE backend covers the whole node, skip CPU/GPU backends entirely. */
  if (!node_active) {
    for (int i = 0; i < NUM_BACKENDS; i++) {
      const pinsight_energy_backend_t *b = all_backends[i];
      if (b->kind == ENERGY_KIND_CPU) {
        /* At most one CPU backend — avoids double-counting where both
         * powercap and amd_energy are present. */
        if (cpu_active)
          continue;
        if (b->init() > 0) {
          active[num_active++] = b;
          cpu_active = 1;
        }
      } else if (b->kind == ENERGY_KIND_GPU) {
        if (b->init() > 0)
          active[num_active++] = b;
      }
    }
  }

  if (num_active == 0)
    fprintf(stderr, "PInsight ENERGY: no readable energy backend on this node; "
                    "energy sequences will be empty\n");
}

void pinsight_energy_fini(void) {
  pinsight_energy_disarm(); /* defensive: never tear down with an open span */
  for (int i = 0; i < num_active; i++)
    active[i]->fini();
  num_active = 0;
}

void pinsight_energy_read(pinsight_energy_t *e) {
  memset(e, 0, sizeof(*e));
  for (int i = 0; i < num_active; i++)
    active[i]->read(e);
}

static void energy_emit(int is_exit, uint64_t seq) {
  pinsight_energy_t e;
  pinsight_energy_read(&e);
  if (is_exit)
    lttng_ust_tracepoint(energy_pinsight_lttng_ust, energy_exit,
                         (unsigned int)e.num_cpu_sockets, e.cpu_energy_uj,
                         (unsigned int)e.num_gpu_devices, e.gpu_energy_uj, seq);
  else
    lttng_ust_tracepoint(energy_pinsight_lttng_ust, energy_enter,
                         (unsigned int)e.num_cpu_sockets, e.cpu_energy_uj,
                         (unsigned int)e.num_gpu_devices, e.gpu_energy_uj, seq);
}

void pinsight_energy_arm(void) {
  if (energy_armed)
    return;
  pinsight_nodepolicy_t pol = energy_resolved_policy();
  if (pol == PINSIGHT_NODEPOLICY_OFF)
    return;
  if (!energy_ever_armed) {
    /* First arm: latch the variant for the whole run and elect this process's
     * role with it. Mid-run variant changes are ignored (per-rank reloads are
     * unsynchronized — a measurer handoff would double-count or drop the
     * node-wide counters). */
    energy_latched_policy = pol;
    energy_measure_active = pinsight_node_role("energy", pol);
    if (energy_measure_active)
      backends_init_once();
    energy_ever_armed = 1;
  } else if (pol != energy_latched_policy) {
    fprintf(stderr,
            "PInsight ENERGY: measure variant change (%s -> %s) ignored — "
            "variant is latched per run; re-arming as %s\n",
            pinsight_nodepolicy_str(energy_latched_policy),
            pinsight_nodepolicy_str(pol),
            pinsight_nodepolicy_str(energy_latched_policy));
  }
  energy_armed = 1;
  if (!energy_measure_active)
    return; /* armed, but not this node's measurer — emit nothing */
  energy_emit(0, energy_span_seq);
}

void pinsight_energy_disarm(void) {
  if (!energy_armed)
    return;
  energy_armed = 0;
  uint64_t s = energy_span_seq++;
  if (!energy_measure_active)
    return;
  /* Final read closes the span (enter/exit pair shares seq) — never a bare stop. */
  energy_emit(1, s);
}

void pinsight_energy_apply_config(void) {
  pinsight_nodepolicy_t pol = energy_resolved_policy();
  if (!energy_armed) {
    if (pol != PINSIGHT_NODEPOLICY_OFF)
      pinsight_energy_arm();
    return;
  }
  if (pol == PINSIGHT_NODEPOLICY_OFF) {
    pinsight_energy_disarm();
    return;
  }
  if (pol != energy_latched_policy) {
    /* Armed and the variant changed: warn once per distinct requested value. */
    static pinsight_nodepolicy_t warned = PINSIGHT_NODEPOLICY_OFF;
    if (pol != warned) {
      warned = pol;
      fprintf(stderr,
              "PInsight ENERGY: [Energy] measure variant change (%s -> %s) "
              "ignored mid-run — takes effect next run\n",
              pinsight_nodepolicy_str(energy_latched_policy),
              pinsight_nodepolicy_str(pol));
    }
  }
}
