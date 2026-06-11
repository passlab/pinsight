---
name: energy-power-support
description: "Energy/power tracing: pluggable backends, AMD-SMI is the working Tuolumne MI300A path; AMD-SMI teardown gotcha; dedicated LTTng provider with sequence fields"
metadata: 
  node_type: memory
  type: project
  originSessionId: 332c5c65-7a48-4d24-943a-9bd851376524
---

Energy/power tracing for PInsight (plan: doc/energy_power_implementation_plan.md — its
"Current Status" section is the authoritative continuation guide, incl. the Tuolumne
evaluation procedure). Branch `energy-power-cleanup`. Feature 1 (enter/exit total energy)
done + validated on Tuolumne (4× MI300A, ROCm 7.2.1, 2026-06-11). **Current phase
(2026-06-11): application evaluation on Tuolumne, not new features.** TODO queue after
that: [Energy] config section → Feature 2 power polling (on/off design decided:
enabled/follow_mode/API) → energy markers → NVML/Level-Zero. Evaluation caveats: MI300A
gpu_uj = combined APU package (no CPU/GPU split); multi-rank nodes duplicate node energy
(dedupe by hostname); idle ~120 W/APU baseline.

**Architecture: pluggable backend seam** (`src/energy_backend.h`). Coordinator
`src/energy.c` owns the dedicated `energy_pinsight_lttng_ust` LTTng provider and activates
backends by policy: NODE supersedes all; at most one CPU (avoids powercap+amd_energy
double-count); all GPU. Backends:
- `energy_sysfs.c` — powercap(intel-rapl) + amd_energy(hwmon), CPU µJ. Both root-locked on
  LC nodes (CVE-2020-8694) → report 0 readable → skipped gracefully.
- `energy_amd_smi.c` — `PINSIGHT_ENERGY_AMD_GPU`, links libamd_smi. **The path that works on
  MI300A as non-root.** `amdsmi_get_energy_count` → µJ = raw*resolution; MI300A returns
  combined CPU+GPU+HBM per-APU package energy (~120 W idle/APU).
- `energy_variorum.c` — `PINSIGHT_ENERGY_VARIORUM`, NODE stub. System Variorum at
  /usr/lib returns UNSUPPORTED_PLATFORM on MI300A, so it's a placeholder only.

**Tracepoints**: `energy_enter`/`energy_exit`, two LTTng dynamic **sequences** `cpu_uj`/
`gpu_uj` (µJ) — no fixed socket/device ceiling; length=#sources, position=index, 0=not
measured. Plus `seq` (0 for enter/exit).

**AMD-SMI teardown GOTCHA (cost hours; do not re-derive):** reading amdsmi on the **main
thread during DSO teardown** (destructor/atexit) **after the constructor already read it**
corrupts the heap — abort `malloc(): unsorted double linked list corrupted` inside
amdsmi's gpu_metrics fread path. atexit doesn't help (a DSO's atexit runs in `_dl_fini`).
A read from a **separate live thread** is clean. Fix: `energy_enter` is read in the
constructor (main thread); `energy_exit` is emitted from the **control thread as it shuts
down** (in `pinsight_control_thread_stop`, after the exit_pinsight marker), never the
destructor. We never call `amdsmi_shut_down()` (unsafe at teardown; OS reclaims).

**Build/validate on Tuolumne**: reconfigure existing `build_omp` (it has the right
LTTng-2.13 ~/local paths; a fresh cmake picks system LTTng 2.8 and fails). Flags:
`-DPINSIGHT_ENERGY_AMD_GPU=TRUE -DROCM_PATH=/opt/rocm-7.2.1`. Test = LD_PRELOAD libpinsight
on any binary under an lttng session, `babeltrace2`. See [[hip-rocm-support]], [[tuolumne-build]].
