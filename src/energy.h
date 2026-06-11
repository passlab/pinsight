//
// PInsight energy/power measurement — interface.
//
// Feature 1 (PINSIGHT_ENERGY): total application energy from two counter reads,
// one at library enter and one at exit. Hardware exposes monotonically
// increasing energy accumulators; the analysis side diffs enter/exit to get
// total joules and average power. See doc/energy_power_implementation_plan.md.
//
// This first slice implements the CPU path via the powercap sysfs interface
// (/sys/class/powercap/intel-rapl/intel-rapl:N/energy_uj), which on current
// kernels is populated by both Intel RAPL and AMD. GPU paths and the [Energy]
// config-section selection are added in later steps; until then all discovered
// sockets are read by default.
//
#ifndef PINSIGHT_ENERGY_H
#define PINSIGHT_ENERGY_H

#include <stdint.h>

#define MAX_ENERGY_PACKAGES 16
#define MAX_ENERGY_GPU_DEVS 16

typedef struct {
  int num_cpu_sockets;
  uint64_t cpu_energy_uj[MAX_ENERGY_PACKAGES]; /* microjoules per socket; 0 if not measured */
  int num_gpu_devices;
  uint64_t gpu_energy_mj[MAX_ENERGY_GPU_DEVS]; /* millijoules per device; 0 if not measured */
} pinsight_energy_t;

/* Discover sysfs counters and probe readability. Called once at library
 * constructor. Safe to call even when no counter is present or readable —
 * reads then simply yield 0. */
void pinsight_energy_init(void);

/* Release any resources held by the energy subsystem. Called once at the
 * library destructor. */
void pinsight_energy_fini(void);

/* Read all enabled counters into *e (zero-initialized first). Cheap enough for
 * the twice-per-run enter/exit use. */
void pinsight_energy_read(pinsight_energy_t *e);

/* Read counters and emit the energy_enter / energy_exit tracepoints. These own
 * the dedicated energy_pinsight_lttng_ust provider, so callers (enter_exit.c)
 * never touch the provider directly — this keeps the provider defined in a
 * single translation unit. */
void pinsight_energy_snapshot_enter(void);
void pinsight_energy_snapshot_exit(void);

#endif /* PINSIGHT_ENERGY_H */
