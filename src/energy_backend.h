//
// PInsight energy — internal pluggable-backend interface.
//
// A backend is a small (init/read/fini) source of energy readings. The
// coordinator in energy.c discovers the compiled-in backends, activates those
// that report readable sources, and on each read lets every active backend
// append its values to the shared pinsight_energy_t.
//
// Activation policy by kind:
//   NODE — supersedes everything: if a NODE backend (e.g. Variorum) is active,
//          CPU and GPU backends are skipped (it already covers the whole node).
//   CPU  — at most one active (first with readable sources), to avoid
//          double-counting on machines exposing both powercap and amd_energy.
//   GPU  — all active backends contribute (distinct devices).
//
#ifndef PINSIGHT_ENERGY_BACKEND_H
#define PINSIGHT_ENERGY_BACKEND_H

#include "energy.h"

typedef enum {
  ENERGY_KIND_CPU = 0,
  ENERGY_KIND_GPU,
  ENERGY_KIND_NODE,
} pinsight_energy_kind_t;

typedef struct pinsight_energy_backend {
  const char *name;
  pinsight_energy_kind_t kind;
  /* Discover sources and acquire handles. Return the number of readable
   * sources (>0 to activate), 0 if available but nothing readable, -1 if the
   * backend is unavailable on this platform. */
  int (*init)(void);
  /* Append this backend's readings to *e (advancing e->num_cpu_sockets /
   * e->num_gpu_devices). Only called on activated backends. */
  void (*read)(pinsight_energy_t *e);
  /* Release handles/fds. Only called on activated backends. */
  void (*fini)(void);
} pinsight_energy_backend_t;

/* Append helpers used by backends — bounds-checked. */
static inline void energy_push_cpu(pinsight_energy_t *e, uint64_t uj) {
  if (e->num_cpu_sockets < MAX_ENERGY_PACKAGES)
    e->cpu_energy_uj[e->num_cpu_sockets++] = uj;
}
static inline void energy_push_gpu(pinsight_energy_t *e, uint64_t uj) {
  if (e->num_gpu_devices < MAX_ENERGY_GPU_DEVS)
    e->gpu_energy_uj[e->num_gpu_devices++] = uj;
}

/* Backend instances (defined in their respective translation units). */
extern const pinsight_energy_backend_t pinsight_energy_backend_powercap;
extern const pinsight_energy_backend_t pinsight_energy_backend_amd_energy;
#ifdef PINSIGHT_ENERGY_AMD_GPU
extern const pinsight_energy_backend_t pinsight_energy_backend_amd_smi;
#endif
#ifdef PINSIGHT_ENERGY_VARIORUM
extern const pinsight_energy_backend_t pinsight_energy_backend_variorum;
#endif

#endif /* PINSIGHT_ENERGY_BACKEND_H */
