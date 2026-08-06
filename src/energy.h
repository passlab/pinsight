//
// PInsight energy/power measurement — public interface.
//
// Energy is measured in ARMED SPANS: each span is bracketed by two counter
// reads emitted as energy_enter/energy_exit (sharing seq = span ordinal) on a
// dedicated LTTng provider. A run that never reconfigures has exactly one span
// (library enter to exit). Energy is gathered through pluggable *backends*
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
#include "trace_config.h" /* pinsight_nodepolicy_t */

/* WHICH ranks measure energy (node-singleton policy). Default OFF since
 * 2026-07-31 — energy is opt-in, independent of domain modes. Set by env
 * PINSIGHT_MEASURE_ENERGY (overrides at every evaluation) or the minimal
 * `[Energy] measure` config key. off <-> armed is switchable at config reload
 * (armed-span model below); the VARIANT (on/anyone_per_node/leader_per_node)
 * is latched at first arm, per run. anyone_per_node/leader_per_node => exactly
 * one rank/node measures (fixes the multi-rank node-energy multi-count).
 * See doc/node_singleton_measurement_design.md and the 2026-07-31 amendment in
 * pinsight-eval/docs/design/energy_power_implementation_plan.md (private dev repo).
 * (Folds into energy_power_config_t when that struct is built.)
 * DECLARED in trace_config.h / DEFINED in trace_config.c (so the standalone
 * config-parser test, which doesn't link energy.c, still resolves it). */

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

/* Read every active backend into *e (zero-initialized first); each backend
 * appends its sources to the cpu/gpu arrays. */
void pinsight_energy_read(pinsight_energy_t *e);

/* ---- Armed-span API (2026-07-31) ----
 * Energy measurement runs in ARMED SPANS: arm takes a counter read and emits
 * energy_enter; disarm takes a FINAL read and emits energy_exit — a span is
 * always completed, never bare-stopped. The enter/exit pair of span i share
 * seq = i (0,1,2,... per process). First arm resolves the node role, latches
 * the policy variant for the run, and initializes backends; backends stay
 * initialized across disarm, so re-arm is just a read. All three functions
 * no-op as appropriate (arm when policy=off or already armed; disarm when not
 * armed; emissions when this process is not the node's measurer).
 *
 * THREADING CONSTRAINT: the startup arm runs in the library constructor (main
 * thread); every mid-run transition and the final disarm MUST run on the
 * control thread, NOT the library destructor. AMD-SMI's gpu_metrics path
 * corrupts the heap if read on the main thread during DSO teardown after the
 * constructor's read (empirically: a teardown read after a constructor read
 * aborts; a read from a separate live thread is clean). */
void pinsight_energy_arm(void);
void pinsight_energy_disarm(void);

/* Control-thread hook, called after each config reload: applies off<->armed
 * transitions from the re-parsed [Energy] measure (env override still wins);
 * warns and ignores mid-run VARIANT changes (latched at first arm). */
void pinsight_energy_apply_config(void);

/* Finalize active backends (disarms first if a span is open). Called from the
 * control thread as it shuts down, NOT a destructor (same AMD-SMI teardown
 * constraint). */
void pinsight_energy_fini(void);

#endif /* PINSIGHT_ENERGY_H */
