//
// PInsight energy/power measurement — public interface.
//
// Total application energy from two counter reads (library enter and exit) on a
// dedicated LTTng provider. Energy is gathered through pluggable *backends*
// (see energy_backend.h): native sysfs (powercap / amd_energy), AMD-SMI for AMD
// GPU/APU, and an optional Variorum backend for portable systems. Each backend
// contributes per-socket / per-device microjoule readings that are emitted as
// two LTTng sequences. See doc/energy_power_implementation_plan.md.
//
#ifndef PINSIGHT_ENERGY_H
#define PINSIGHT_ENERGY_H

#include <stdint.h>

/* Brings in PINSIGHT_ENERGY_AMD_GPU / PINSIGHT_ENERGY_VARIORUM so every energy
 * translation unit (coordinator and backends) sees which backends are enabled. */
#include "pinsight_config.h"

#define MAX_ENERGY_PACKAGES 16
#define MAX_ENERGY_GPU_DEVS 16

/* All energy values are microjoules. CPU readings come from CPU-package
 * backends; GPU/accelerator readings (incl. the MI300A combined APU package)
 * come from GPU/node backends. num_* are the active lengths of each array. */
typedef struct {
  int num_cpu_sockets;
  uint64_t cpu_energy_uj[MAX_ENERGY_PACKAGES];
  int num_gpu_devices;
  uint64_t gpu_energy_uj[MAX_ENERGY_GPU_DEVS];
} pinsight_energy_t;

/* Discover and initialize all compiled-in backends. Called once from the
 * library constructor (main thread). Safe when no backend is present or
 * readable — reads then yield an empty sequence rather than failing. */
void pinsight_energy_init(void);

/* Read every active backend into *e (zero-initialized first); each backend
 * appends its sources to the cpu/gpu arrays. */
void pinsight_energy_read(pinsight_energy_t *e);

/* Read counters and emit the energy_enter / energy_exit tracepoints. These own
 * the dedicated energy_pinsight_lttng_ust provider, so callers never touch the
 * provider directly — it stays defined in one translation unit.
 *
 * THREADING CONSTRAINT: energy_enter is emitted from the constructor (main
 * thread); energy_exit MUST be emitted from the control thread as it shuts down,
 * NOT from the library destructor. AMD-SMI's gpu_metrics path corrupts the heap
 * if read on the main thread during DSO teardown after the constructor's read
 * (empirically: a teardown read after a constructor read aborts; a read from a
 * separate live thread is clean). The control thread is such a thread. */
void pinsight_energy_snapshot_enter(void);
void pinsight_energy_snapshot_exit(void);

/* Finalize active backends. Called from the control thread alongside
 * snapshot_exit, NOT a destructor (same AMD-SMI teardown constraint). */
void pinsight_energy_fini(void);

#endif /* PINSIGHT_ENERGY_H */
