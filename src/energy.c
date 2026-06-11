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
#include <string.h>

#include "energy.h"
#include "energy_backend.h"

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

void pinsight_energy_init(void) {
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
  for (int i = 0; i < num_active; i++)
    active[i]->fini();
  num_active = 0;
}

void pinsight_energy_read(pinsight_energy_t *e) {
  memset(e, 0, sizeof(*e));
  for (int i = 0; i < num_active; i++)
    active[i]->read(e);
}

void pinsight_energy_snapshot_enter(void) {
  pinsight_energy_t e;
  pinsight_energy_read(&e);
  lttng_ust_tracepoint(energy_pinsight_lttng_ust, energy_enter,
                       (unsigned int)e.num_cpu_sockets, e.cpu_energy_uj,
                       (unsigned int)e.num_gpu_devices, e.gpu_energy_uj, (uint64_t)0);
}

void pinsight_energy_snapshot_exit(void) {
  pinsight_energy_t e;
  pinsight_energy_read(&e);
  lttng_ust_tracepoint(energy_pinsight_lttng_ust, energy_exit,
                       (unsigned int)e.num_cpu_sockets, e.cpu_energy_uj,
                       (unsigned int)e.num_gpu_devices, e.gpu_energy_uj, (uint64_t)0);
}
