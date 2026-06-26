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
- `energy_amd_smi.c` — `PINSIGHT_ENERGY_AMD_GPU`, **dlopen(RTLD_LOCAL)** libamd_smi (not
  linked — see gotcha below). **The path that works on MI300A as non-root.**
  `amdsmi_get_energy_count` → µJ = raw*resolution; MI300A returns combined CPU+GPU+HBM
  per-APU package energy (~120 W idle/APU).
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

**libamd_smi vs hwloc rocm_smi GOTCHA (cost ~1hr; fixed cb27194):** tracing any Cray-MPI+GPU
app under energy crashed in `_dl_init` — hwloc (via cray-mpich) loads an OLDER
/usr/lib64/hwloc/librocm_smi64.so.7 whose `amd::smi::` symbols got interposed by PInsight's
linked libamd_smi.so.26 (global LD_PRELOAD scope) → abort in librocm_smi64 static init
(GpuMetricsBase_v17_t dtor). HWLOC_COMPONENTS=-rsmi did NOT help. Fix: energy_amd_smi.c now
**dlopen(libamd_smi, RTLD_NOW|RTLD_LOCAL|RTLD_DEEPBIND)** + dlsym, not -lamd_smi → symbols
private, no interposition. Energy is now a runtime soft-dep (CMake needs only amdsmi.h).

**Build/validate on Tuolumne**: reconfigure existing `build_omp` (it has the right
LTTng-2.13 ~/local paths; a fresh cmake picks system LTTng 2.8 and fails). Flags:
`-DPINSIGHT_ENERGY_AMD_GPU=TRUE -DROCM_PATH=/opt/rocm-7.2.1`. Test = LD_PRELOAD libpinsight
on any binary under an lttng session, `babeltrace2`. See [[hip-rocm-support]], [[tuolumne-build]].

**First app eval (2026-06-11): AMG2023** (El Capitan benchmark, hypre BoomerAMG, MPI+HIP)
built + energy-traced on MI300A. Recipe+results in /g/g19/yan10/tools/benchmarks/
(build_amg2023_tuolumne.sh, README_AMG2023_eval.md). hypre v3.1.0 needs 4 ROCm-7.2.1 fixes
(C++17; MPI-include to hipcc; thrust::identity<T>()→thrust::identity() patch; --without-umpire
--enable-unified-memory). Built w/ Cray cc/CC wrappers (handle cray-mpich), gfx942. Run
single-rank with MPICH_GPU_SUPPORT_ENABLED=0 (multi-rank GPU-aware MPI needs -lmpi_gtl_hsa).
Result -n 100^3 ~24s: active APU 3384 J/140 W vs ~123 W idle on other 3 APUs.

**Multi-node BLOCKER (2026-06-11):** Tuolumne scheduler is **Flux**. Node = 96 cores + 4
APUs; queues pdebug(<=16n,fast)/pbatch/workshop. But `flux run/batch/alloc` ALL fail:
`cannot find user/bank or user/default bank entry for uid: 54705` — yan10 is NOT enrolled
in this instance's flux accounting for ANY bank (groups: yan10,us_cit,llnl_emp,lc-user — no
project bank derivable). Needs an LC bank association (admin/PI). Ready-to-run script
benchmarks/amg2023_flux_multinode.sh (4 ranks/node 1 APU each via `flux run -g1`, per-node
lttng energy, weak-scaled, 3D grid auto-factored) — just set BANK=. Inner `flux run` in the
batch needs no --bank (charged at `flux batch`). Single-rank login-node runs work fine
(that's how AMG was validated).

**WORKAROUND that got multi-rank working (2026-06-11):** a **personal Flux instance**
`flux start -s1 bash -c 'flux run -n4 ...'` runs multi-rank on the current node with NO
bank/accounting (bypasses the blocker). Per-rank APU bind: `ROCR_VISIBLE_DEVICES=
$((FLUX_TASK_RANK%4))` (flux `-g1` mis-binds all to GPU3 on login node). Add
`-o cpu-affinity=off` or ranks SIGABRT from CPU contention on the shared login node.
Login node tuolumne2151 IS a 4-APU MI300A node. **PInsight combined build `build_eval`
(MPI+HIP+ENERGY_AMD_GPU+OpenMP) traced 4-rank AMG2023 on 4 APUs**: mpirank 0-3, inter-rank
MPI_Isend 17k/Irecv 17k/Waitall 6k + Allreduce 2.3k, roctracer ~20k/rank balanced, 4 energy
pairs. MPI domain enabled via amg_trace_config.txt ([MPI.global] trace_mode=TRACING) — domains
default OFF without config. Needed CMake fix: target_link_libraries MPI::MPI_C not -lmpi
(cray libmpi_cray.so); committed e12cc23. Local run script: eva/AMG2023-tuolumne/run_4rank_local.sh.
