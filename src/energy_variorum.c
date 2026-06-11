//
// PInsight energy — Variorum backend (PLACEHOLDER STUB).
//
// Variorum (https://variorum.readthedocs.io) is LLNL's vendor-agnostic power
// API. It is kept here as a pluggable NODE backend for portable systems where
// it is built for the platform and the native counters are unavailable.
//
// STATUS: stub. The system Variorum on Tuolumne returns
// _ERROR_VARIORUM_UNSUPPORTED_PLATFORM on MI300A (gfx942), so the working
// Tuolumne path is the AMD-SMI backend (energy_amd_smi.c), not this one. When a
// Variorum build supports the target platform, implement init/read here using
// variorum_get_energy_json() + a JSON parse, returning the node energy. As a
// NODE backend it supersedes the CPU/GPU backends when active (see energy_backend.h).
//
#include "pinsight_config.h" /* must precede the #ifdef so the macro is defined */

#ifdef PINSIGHT_ENERGY_VARIORUM

#include <stdio.h>

#include "energy_backend.h"

static int variorum_init(void) {
  /* Not yet implemented — report unavailable so the coordinator falls back to
   * the native CPU/GPU backends. */
  fprintf(stderr, "PInsight ENERGY: variorum backend is a stub (not implemented); "
                  "using native backends\n");
  return -1;
}

static void variorum_read(pinsight_energy_t *e) {
  (void)e; /* never called while init() returns -1 */
}

static void variorum_fini(void) {}

const pinsight_energy_backend_t pinsight_energy_backend_variorum = {
    .name = "variorum(node)",
    .kind = ENERGY_KIND_NODE,
    .init = variorum_init,
    .read = variorum_read,
    .fini = variorum_fini,
};

#endif /* PINSIGHT_ENERGY_VARIORUM */
