//
// PInsight energy — AMD-SMI backend (AMD GPU / MI300A APU).
//
// Reads each AMD processor's monotonic energy accumulator via
// amdsmi_get_energy_count(). On MI300A this returns the combined CPU+GPU+HBM
// APU package energy — the hardware exposes no per-component breakdown — so a
// 4-APU node yields four readings. Energy in microjoules = raw * counter_resolution.
//
// Validated on Tuolumne (4x MI300A gfx942, ROCm 7.2.1) as a non-root user.
//
// IMPORTANT — why dlopen with RTLD_LOCAL instead of linking -lamd_smi:
// libamd_smi exports C++ symbols in the amd::smi:: namespace that COLLIDE with
// the older librocm_smi64.so that HPC stacks load indirectly (e.g. hwloc's GPU
// topology plugin, pulled in by Cray MPI). If libamd_smi is a global-scope
// NEEDED dependency (via -lamd_smi + LD_PRELOAD), its symbols interpose
// librocm_smi64's and the app crashes in librocm_smi64's static init
// (amd::smi::GpuMetricsBase_v17_t::~GpuMetricsBase_v17_t). Loading libamd_smi
// with RTLD_LOCAL keeps its symbols private to this backend, so the host stack's
// rocm_smi is undisturbed. This also makes energy a runtime soft-dependency:
// PInsight links fine and runs without libamd_smi present.
//
#include "pinsight_config.h" /* must precede the #ifdef so the macro is defined */

#ifdef PINSIGHT_ENERGY_AMD_GPU

#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>

#include <amd_smi/amdsmi.h> /* types/enums only; functions are resolved by dlsym */

#include "energy_backend.h"

/* Function-pointer types for the amdsmi entry points we use. */
typedef amdsmi_status_t (*fn_init_t)(uint64_t);
typedef amdsmi_status_t (*fn_socket_handles_t)(uint32_t *, amdsmi_socket_handle *);
typedef amdsmi_status_t (*fn_processor_handles_t)(amdsmi_socket_handle, uint32_t *,
                                                  amdsmi_processor_handle *);
typedef amdsmi_status_t (*fn_energy_count_t)(amdsmi_processor_handle, uint64_t *,
                                             float *, uint64_t *);

static void *amd_smi_dl = NULL;
static fn_init_t p_init = NULL;
static fn_socket_handles_t p_sockets = NULL;
static fn_processor_handles_t p_procs = NULL;
static fn_energy_count_t p_energy = NULL;

static amdsmi_processor_handle amd_smi_procs[MAX_ENERGY_GPU_DEVS];
static int amd_smi_n = 0;

/* Load libamd_smi privately (RTLD_LOCAL so its symbols never interpose the
 * host stack's rocm_smi) and resolve the entry points. Returns 0 on success. */
static int amd_smi_dlopen(void) {
  const char *names[] = {"libamd_smi.so", "libamd_smi.so.26", "libamd_smi.so.25",
                         "libamd_smi.so.24"};
  for (unsigned i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
    amd_smi_dl = dlopen(names[i], RTLD_NOW | RTLD_LOCAL | RTLD_DEEPBIND);
    if (amd_smi_dl)
      break;
  }
  if (!amd_smi_dl) {
    fprintf(stderr, "PInsight ENERGY: amd_smi backend: libamd_smi not loadable "
                    "(%s); GPU/APU energy disabled\n", dlerror());
    return -1;
  }
  p_init = (fn_init_t)dlsym(amd_smi_dl, "amdsmi_init");
  p_sockets = (fn_socket_handles_t)dlsym(amd_smi_dl, "amdsmi_get_socket_handles");
  p_procs = (fn_processor_handles_t)dlsym(amd_smi_dl, "amdsmi_get_processor_handles");
  p_energy = (fn_energy_count_t)dlsym(amd_smi_dl, "amdsmi_get_energy_count");
  if (!p_init || !p_sockets || !p_procs || !p_energy)
    return -1;
  return 0;
}

static int amd_smi_init(void) {
  if (amd_smi_dlopen() != 0)
    return -1;
  if (p_init(AMDSMI_INIT_AMD_GPUS) != AMDSMI_STATUS_SUCCESS)
    return -1;

  uint32_t sc = 0;
  if (p_sockets(&sc, NULL) != AMDSMI_STATUS_SUCCESS || sc == 0)
    return -1;
  amdsmi_socket_handle socks[64];
  if (sc > 64)
    sc = 64;
  p_sockets(&sc, socks);

  amd_smi_n = 0;
  for (uint32_t s = 0; s < sc && amd_smi_n < MAX_ENERGY_GPU_DEVS; s++) {
    uint32_t pc = 0;
    if (p_procs(socks[s], &pc, NULL) != AMDSMI_STATUS_SUCCESS)
      continue;
    amdsmi_processor_handle procs[64];
    if (pc > 64)
      pc = 64;
    p_procs(socks[s], &pc, procs);
    for (uint32_t p = 0; p < pc && amd_smi_n < MAX_ENERGY_GPU_DEVS; p++)
      amd_smi_procs[amd_smi_n++] = procs[p];
  }

  int readable = 0;
  for (int i = 0; i < amd_smi_n; i++) {
    uint64_t raw = 0, ts = 0;
    float res = 0;
    if (p_energy(amd_smi_procs[i], &raw, &res, &ts) == AMDSMI_STATUS_SUCCESS)
      readable++;
  }
  if (readable == 0)
    return 0;
  fprintf(stderr, "PInsight ENERGY: amd_smi monitoring %d AMD processor(s) "
                  "(APU/GPU package energy)\n", amd_smi_n);
  return amd_smi_n;
}

static void amd_smi_read(pinsight_energy_t *e) {
  for (int i = 0; i < amd_smi_n; i++) {
    uint64_t raw = 0, ts = 0, uj = 0;
    float res = 0;
    if (p_energy(amd_smi_procs[i], &raw, &res, &ts) == AMDSMI_STATUS_SUCCESS)
      uj = (uint64_t)((double)raw * (double)res);
    energy_push_gpu(e, uj);
  }
}

static void amd_smi_fini(void) {
  /* Deliberately do NOT call amdsmi_shut_down() and do NOT dlclose(): calling
   * into libamd_smi during process teardown corrupts the heap (it destructs
   * before us); leaving it loaded is clean — the OS reclaims at exit. The exit
   * energy read runs from the control thread (a live thread); see energy.c. */
}

const pinsight_energy_backend_t pinsight_energy_backend_amd_smi = {
    .name = "amd_smi(APU/GPU)",
    .kind = ENERGY_KIND_GPU,
    .init = amd_smi_init,
    .read = amd_smi_read,
    .fini = amd_smi_fini,
};

#endif /* PINSIGHT_ENERGY_AMD_GPU */
