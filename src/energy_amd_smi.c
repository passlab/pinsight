//
// PInsight energy — AMD-SMI backend (AMD GPU / MI300A APU).
//
// Reads each AMD processor's monotonic energy accumulator via
// amdsmi_get_energy_count(). On MI300A this returns the combined CPU+GPU+HBM
// APU package energy — the hardware exposes no per-component breakdown — so a
// 4-APU node yields four readings. Energy in microjoules = raw * counter_resolution.
//
// Validated on Tuolumne (4x MI300A gfx942, ROCm 7.2.1) as a non-root user:
// amdsmi_init/get_energy_count succeed and return real per-APU energy, unlike
// the root-locked RAPL/amd_energy sysfs counters.
//
#include "pinsight_config.h" /* must precede the #ifdef so the macro is defined */

#ifdef PINSIGHT_ENERGY_AMD_GPU

#include <stdint.h>
#include <stdio.h>

#include <amd_smi/amdsmi.h>

#include "energy_backend.h"

static amdsmi_processor_handle amd_smi_procs[MAX_ENERGY_GPU_DEVS];
static int amd_smi_n = 0;
static int amd_smi_started = 0;

static int amd_smi_init(void) {
  if (amdsmi_init(AMDSMI_INIT_AMD_GPUS) != AMDSMI_STATUS_SUCCESS)
    return -1;
  amd_smi_started = 1;

  uint32_t sc = 0;
  if (amdsmi_get_socket_handles(&sc, NULL) != AMDSMI_STATUS_SUCCESS || sc == 0) {
    amdsmi_shut_down();
    amd_smi_started = 0;
    return -1;
  }
  amdsmi_socket_handle socks[64];
  if (sc > 64)
    sc = 64;
  amdsmi_get_socket_handles(&sc, socks);

  amd_smi_n = 0;
  for (uint32_t s = 0; s < sc && amd_smi_n < MAX_ENERGY_GPU_DEVS; s++) {
    uint32_t pc = 0;
    if (amdsmi_get_processor_handles(socks[s], &pc, NULL) != AMDSMI_STATUS_SUCCESS)
      continue;
    amdsmi_processor_handle procs[64];
    if (pc > 64)
      pc = 64;
    amdsmi_get_processor_handles(socks[s], &pc, procs);
    for (uint32_t p = 0; p < pc && amd_smi_n < MAX_ENERGY_GPU_DEVS; p++)
      amd_smi_procs[amd_smi_n++] = procs[p];
  }

  int readable = 0;
  for (int i = 0; i < amd_smi_n; i++) {
    uint64_t raw = 0, ts = 0;
    float res = 0;
    if (amdsmi_get_energy_count(amd_smi_procs[i], &raw, &res, &ts) ==
        AMDSMI_STATUS_SUCCESS)
      readable++;
  }
  if (readable == 0) {
    amdsmi_shut_down();
    amd_smi_started = 0;
    return 0;
  }
  fprintf(stderr, "PInsight ENERGY: amd_smi monitoring %d AMD processor(s) "
                  "(APU/GPU package energy)\n", amd_smi_n);
  return amd_smi_n;
}

static void amd_smi_read(pinsight_energy_t *e) {
  for (int i = 0; i < amd_smi_n; i++) {
    uint64_t raw = 0, ts = 0, uj = 0;
    float res = 0;
    if (amdsmi_get_energy_count(amd_smi_procs[i], &raw, &res, &ts) ==
        AMDSMI_STATUS_SUCCESS)
      uj = (uint64_t)((double)raw * (double)res);
    energy_push_gpu(e, uj);
  }
}

static void amd_smi_fini(void) {
  /* Deliberately do NOT call amdsmi_shut_down(): calling into libamd_smi during
   * process teardown corrupts the heap (libamd_smi destructs before us), and
   * leaving it initialized is clean — the OS reclaims everything at exit. The
   * exit energy read runs from an atexit() handler (before DSO teardown), where
   * amdsmi is still valid; see energy.c. */
  (void)amd_smi_started;
}

const pinsight_energy_backend_t pinsight_energy_backend_amd_smi = {
    .name = "amd_smi(APU/GPU)",
    .kind = ENERGY_KIND_GPU,
    .init = amd_smi_init,
    .read = amd_smi_read,
    .fini = amd_smi_fini,
};

#endif /* PINSIGHT_ENERGY_AMD_GPU */
