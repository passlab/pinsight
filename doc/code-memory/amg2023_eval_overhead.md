---
name: amg2023-eval-overhead
description: "AMG2023-tuolumne eval: config-parser subdomain-section gotcha, default=unlimited tracing, and the full/rate50/notrace overhead result"
metadata: 
  node_type: memory
  type: project
  originSessionId: 402660e4-1b25-4371-b474-57b3c3435b09
---

AMG2023-tuolumne eval (eva/AMG2023-tuolumne/, git-ignored). Analysis tooling +
overhead study on 4× MI300A via personal Flux instance. See [[hip-rocm-support]],
[[energy-power-support]].

**Config parser does NOT handle `[HIP(subdomain).default]` sections (verified 2026-06-25).**
`parse_section_header` resets `current_domain_idx=-1` per header, then matches the
`.default` branch and calls `find_domain_index("HIP(memcpy)")` → exact strcmp → -1 →
idx stays -1 → the ENTIRE section body is silently skipped (no warning). So the old
`amg_trace_config.txt` (per-subdomain `[HIP(device).default]`, `[HIP(context).default]`…)
was a complete no-op: HIP events traced == install defaults (proved byte-identical to
`[HIP.global] TRACING` alone, and via vecadd discriminator: `HIP_kernel_launch=off`
under `[HIP.default]`→0 launches; under `[HIP(GARBAGE).default]`→unchanged 10).
**Fix:** put all events under ONE `[HIP.default]` section (events looked up by name in
the resolved domain). Only events roctracer_callback.c actually emits are honored:
kernel_launch, memcpy_{HtoD,DtoH,DtoD,HtoH,async}, stream/device_synchronize; the other
~44 of 52 HIP IDs are not implemented (enabling = no-op). hipMalloc/hipFree NOT emitted.

**Default tracing = unlimited.** DEFAULT_TRACE_MAX=-1, _START=0, _RATE=1
(trace_config.h). trace-bit (pinsight.c:349): `max_num_traces==-1 || trace_counter<max`
→ every region, every invocation. Rate-limit via `[Lexgion.default] max_num_traces=N`
(triple lives in Lexgion sections, applies to all regions). Verified: N=3 on vecadd's
10 same-kernel launches → 3 host launch events.

**Overhead result (notrace vs full vs rate50, AMG Setup/Solve phase time + FOM):**
- n=30³/rank: full +17%, rate50 +20% — INDISTINGUISHABLE (run too short).
- n=60³/rank REPS=5, quiet node, updated lib (AUTHORITATIVE): notrace 3.12s; full +8.0%;
  rate50 +12.3% (≈1σ from full, NO runtime benefit, marginally worse). An earlier REPS=3
  n=60 run showed full +55%/rate50 +8% but was CONTENDED-node noise (setup σ≈1.3s,
  outliers to 6s) — discard it.
- **rate50 cuts trace VOLUME ~58% (MPI 81%) but gives NO runtime benefit on this MPI+HIP
  workload** at either size: full overhead (~8%) is dominated by un-rate-limited GPU
  ACTIVITY capture + interception + MPI, not host-tracepoint volume; per-call rate
  bookkeeping ~cancels the host-tracepoint savings. Runtime benefit would need activity
  records rate-limited too (correlation-id gating) or a host-tracepoint-bound workload.
  Win of rate control here = data/storage, not speed.
- **rate50 + `trace_mode_after = HIP:MONITORING` (2026-06-25):** max_num_traces alone does
  NOT switch the domain mode (0 auto-triggers; stays TRACING → activity stays on). Adding
  trace_mode_after makes HIP→MONITORING once the first region hits 50 → activity capture
  STOPS via the mode gate (26k→~420 activity records/run). Overhead drops full +8.5% →
  rate50+modeswitch +2.9% (median; notrace baseline had 2 contention outliers, use
  median/min). THIS is the lever that makes rate control reduce HIP runtime overhead.
  Caveat: trace_mode_after is a GLOBAL one-shot (first region to hit cap flips whole
  domain) = warmup-then-count-only, NOT per-region rate-50. True per-region activity
  rate-limit still needs correlation-id gating (unimplemented). Note: setting it via the
  rate50 config = eva/AMG2023-tuolumne/amg_trace_config_rate50.txt.
- **GPU activity records (hipKernelActivity/hipMemcpyActivity) are NOT rate-limited**
  (ROCTracer activity pool is independent of host-callback enable bits AND of
  max_num_traces) → floor on HIP overhead under rate control. Leaking some under rate
  control is accepted by design.
- **Activity-record mode handling (added/refined 2026-06-25), two mechanisms:**
  (1) **Capture tied to TRACING, not ACTIVE.** Previously activity collection
  (roctracer enable/disable_domain_activity) was tied to ACTIVE, so MONITORING kept
  CAPTURING records → filled the 2MB pool → fired flush callbacks repeatedly = pure
  overhead (defeats MONITORING's count-only purpose; "consume to NULL" does NOT help —
  capture cost already paid). Fix: `pinsight_control_hip_apply_mode` enables collection
  only entering TRACING and disables it leaving TRACING; the pool stays OPEN across
  MONITORING (tied to ACTIVE) so MONITORING↔TRACING only toggles the activity domain, no
  2MB malloc churn. Init enables collection only for a TRACING start.
  (2) **Emit gate `static volatile int hip_activity_emit`.** `hip_activity_callback`
  (pool buffer_callback, fires on buffer-full or explicit flush) early-returns unless the
  flag is set — NOT the live mode: the control thread sets mode BEFORE apply_mode, so a
  live-mode gate would DROP in-pool TRACING records flushed after a TRACING→MONITORING
  switch. apply_mode flushes at the leaving-TRACING boundary while emit==1 (emits the
  TRACING batch) THEN clears emit and disables collection. Fini guards disable on
  emit==1 (avoid double-disable when ending in MONITORING). Verified: static MONITORING/
  STANDBY=0 activity (no leak, no capture); TRACING unchanged (40 act/10 launch); dynamic
  trace_mode_after=HIP:MONITORING mid-run → hipKernelActivity==hipKernelLaunch (TRACING
  records preserved), only async in-flight tail dropped (memcpy 7 host vs 6 act). Only the
  control thread + Init write the flag.
- **Cyclic mode verified (2026-06-25):** drove TRACING↔MONITORING repeatedly on a
  long-running HIP loop via SIGUSR1 config reload (rewrite [HIP.global] trace_mode +
  `kill -USR1 <app_pid>`). Activity pool disable→re-enable→disable cycle works: app exits
  0 (no crash), host launches show clean ON/OFF/ON/OFF windows, and hipKernelActivity ==
  hipKernelLaunch_begin exactly (every TRACING-window kernel captured, incl. after
  MONITORING→TRACING re-enable; nothing leaked in MONITORING). TEST GOTCHA: run the app
  DIRECTLY so `$!` is its PID — wrapping in `timeout`/`stdbuf` makes `$!` the wrapper,
  and SIGUSR1 kills the wrapper (exit 138). PInsight's SIGUSR1 handler survives ROCm init
  (SigCgt bit9 set; control thread logs "reloading config"). Test files in scratchpad:
  looping.hip (kernel+sync+usleep per iter).
- **Committed to repo (2026-06-25):** test/rocm/looping_pinsight.hip (long-running HIP
  loop) + test/rocm/cyclic_mode_test.sh (drives TRACING↔MONITORING via SIGUSR1, asserts
  no-crash + hipKernelActivity==hipKernelLaunch) + Makefile targets `make looping_pinsight`
  / `make cyclic`. test/rocm is NOT git-ignored (unlike eva/).
- **Canonical eval re-run (2026-06-25):** full 4-rank/4-APU AMG2023 at `-n 60^3` with the
  updated lib (corrected HIP config, malloc/free tracing, TRACING-only activity capture).
  Figures regenerated → eva/AMG2023-tuolumne/figures/amg4n60_* (+ ANALYSIS_amg4n60.md).
  Load imbalance MPI 112%/GPU-exec 81%; rank0 barely MPI-waits, ranks2-3 wait ~1.5s;
  launch-bound (launch ~2s/rank vs GPU-exec ~0.4s); energy 160-175 W/APU; top kernels
  csrmvn(SpMV) 38% + rocprim 35%. Superseded n=30 figures removed. Note: hipFree now
  ~11k events/run (default-on). Other build dirs (build_hip/build_omp) NOT yet rebuilt
  with these src changes; only build_eval is current.
- **hipMalloc/hipFree callback tracing added (2026-06-25):** new tracepoints
  hipMalloc_begin(size)/hipMalloc_end(dev_ptr,rv), hipFree_begin(dev_ptr)/hipFree_end(rv)
  in roctracer_lttng_ust_tracepoint.h; HIP_malloc/HIP_free install-status 0→1 in
  trace_domain_HIP.h (NOTE: install-status == default-on, so they now trace by default
  when HIP active — add `HIP_malloc=off`/`HIP_free=off` to keep a baseline); handlers +
  cids in fast-reject allowlist in roctracer_callback.c. hip_api_data_t fields:
  args.hipMalloc.{ptr(void**),size}, args.hipFree.ptr(void*); dev address = *ptr at EXIT.
- **Cold-start:** first traced run inits GPU/LTTng, ~5× slower (one un-warmed run gave a
  spurious +389%). Harness discards a warm-up per mode.

**Eval files** (all git-ignored under eva/AMG2023-tuolumne/): analyze_amg.py (per-rank
MPI/GPU/energy/kernels + 3 figures; devId=rank+4 maps activity→rank), overhead_experiment.sh
(LOCAL_N/REPS/OUTDIR env-tunable), amg_trace_config.txt (corrected full),
amg_trace_config_rate50.txt, ANALYSIS_amg4n30.md, OVERHEAD_results.md.

## Multi-node (Flux) — first real run, 2026-07-16

**Flux bank blocker resolved**: uid now enrolled, bank `asccasc`, queue `pdebug` confirmed
working (`flux batch -q pdebug --bank=asccasc` succeeds). `amg2023_flux_multinode.sh` and
`run_4rank_local.sh` were already written (pre-dating this session) for exactly this —
per-node LTTng session writing to `$OUT/<hostname>/`, `flux run -g1` binds one APU/rank.

**Critical bug found + fixed: `LTTNG_HOME` must be node-local, not the default `$HOME`.**
Tuolumne's `$HOME` (`/g/g19/yan10`) is NFS (`cz-nfs-02-new.llnl.gov:/cz_g19`), shared across
every node. `lttng-sessiond` defaults to keeping its lock/socket state under
`$LTTNG_HOME` (falls back to `$HOME`) — so multiple nodes' session daemons collide on the
*same* shared lock file (`Could not get lock file .../lttng-sessiond.lck, another instance
is running`). This would have broken the real 4-node job identically to how it broke a
1-node calibration run (caught there first, cheaply). **Fix, in both scripts**: each node's
`flux run --tasks-per-node=1` block now does
`export LTTNG_HOME=/tmp/$USER/lttng_home; mkdir -p "$LTTNG_HOME"; pkill -x lttng-sessiond
2>/dev/null || true` before any `lttng` command — control-plane state goes node-local
(`/tmp`), while trace **output** stays on shared FS via `--output=$OUT/$H` as before. Must be
set in EVERY separate `flux run` block that calls `lttng` (create AND the later stop/destroy
block — they're independent bash invocations, don't inherit each other's env).

**Problem-size calibration (1 node, 4 ranks/GPUs, weak-scaled `-n L L L -P 1 2 2`)** — see
`calib/` (calib_job.sh, parse_energy.py, out/L{100,200,300,400}):
| L | elapsed | per-GPU power (avg) |
|---|---|---|
| 100 | 12.6s | 171 W |
| 200 | 69.2s | 168 W |
| 300 | 242.1s | 163 W (AMG Setup phase alone: 165.5s) |
| 400 | cancelled | AMG Setup still unfinished after 10+ min — real cliff, not gradual |

**Key finding: GPU power is FLAT (~160-190 W/APU) across L=100→300, if anything drifting
slightly down.** AMG2023 is NOT power/compute-bound on MI300A in this range — consistent
with AMG being bandwidth/irregular-access-bound, not FLOP-bound. So "increase L to saturate
the GPUs" is the wrong lever — there's no power plateau to chase. (Matches the earlier
n=60 canonical single-node run's 160-175 W/APU — good cross-check consistency.) The real
constraint is AMG **Setup time exploding non-linearly** past L=300. **Chose LOCAL_N=200**
for the production multi-node run: same power profile as 100/300, ~69s is long enough for
real inter-node MPI halo-exchange to show up, well clear of the L=300+ setup-time cliff.
Updated `amg2023_flux_multinode.sh`'s default accordingly.

**Tool-use gotcha**: `flux jobs -a` TIME column plus polling — don't `sleep` past ~110s in one
shell call (hits the harness's own command timeout); poll in shorter increments instead.

## Multi-node — SUCCESS 2026-07-16, two more architectural bugs found+fixed en route

First fully working real 4-node/16-GPU run (`BANK=asccasc QUEUE=pdebug NODES=4
RANKS_PER_NODE=4`), after the LTTNG_HOME fix above plus two more bugs, both found by
watching per-node event counts go from 0 → real numbers:

**Bug 2 — session daemon dies when its originating `flux run` task's cgroup is torn
down**, even though `lttng-sessiond` tries to daemonize (double-fork/detach doesn't
survive a cgroup kill). The original 3-step design (separate `flux run --tasks-per-node=1`
for session-start, then a separate `flux run -n$NRANKS -g1` for AMG, then a third
`flux run --tasks-per-node=1` for stop/destroy) is fundamentally broken: step 1's daemon
is dead before step 2 (AMG) ever launches. Symptom was insidious — `lttng create`/
`enable-event`/`start` all succeeded with zero errors, AMG ran and produced correct FOM
numbers, but **every node's trace was completely empty** (bare `ust/` dir, no `uid/`
subtree). Confirmed via isolated diagnostic (calib/diag3_job.sh): start session in one
`flux run` task, `pgrep lttng-sessiond` in a *separate* follow-up task → `NOT RUNNING`.
**Fix**: merge session-start + AMG-run + session-stop into a **single**
`flux run -N$NODES -n$NRANKS -g1` invocation (one task per rank, matching the AMG launch
topology) so the daemon's lifetime is scoped to the task that also runs the app. Every
local rank on a node attempts create/enable/start and later stop/destroy — no node-local
lock/coordination needed, since `lttng create` on a duplicate name and `lttng destroy` on
an already-gone session both fail harmlessly (suppressed `>/dev/null 2>&1`), so only the
first-to-arrive/first-to-finish rank's attempt actually matters. Relies on MPI's
collective `MPI_Finalize` keeping the 4 local ranks' completion times close together so
the fastest rank's teardown doesn't cut off its siblings' trailing events — worked
cleanly in practice.

**Bug 3 — `LTTNG_HOME` must be set for the traced APPLICATION too, not just the `lttng`
CLI.** After fixing bug 2, sessions still traced zero events (same empty-`ust/`-dir
symptom) even with no errors anywhere. Root cause: the `env -i ... LTTNG_HOME=/tmp/...`
wrapper was only used for the `lttng` control commands; the actual
`env LD_PRELOAD=libpinsight.so ... $AMG ...` invocation never got `LTTNG_HOME` set, so
liblttng-ust (linked into libpinsight.so) fell back to the default (`$HOME`, the shared
NFS path) to find the sessiond's rendezvous socket — a completely different location from
the node-local sessiond the CLI had just started. App and control-plane were talking to
two different (non-existent, from each other's perspective) daemons; UST tracepoints
silently no-op when no reachable sessiond is found, hence zero events with zero errors.
**Fix**: add `LTTNG_HOME=/tmp/$USER/lttng_home` directly to the AMG launch's `env` line,
matching the value used for the CLI wrapper.

**Bug 4 (found 2026-07-17, via TraceCompass showing only 1 GPU/node): `-g1` does NOT
rotate distinct GPUs across local ranks on this Flux instance.** Confirmed via a minimal
diagnostic (`calib/diag5_job.sh`: `flux run -N4 -n16 -g1 ... echo $ROCR_VISIBLE_DEVICES`)
— **every one of the 16 ranks across all 4 nodes got `ROCR_VISIBLE_DEVICES=3`**, i.e. all
4 local ranks/node bound to the SAME physical GPU; the other 3 GPUs/node never ran
anything. This is a real compute-placement bug, not just a trace-reporting artifact —
confirmed via babeltrace2: host-side `hipKernelLaunch_begin` events split evenly across
all 4 `mpirank`s (~9700-9900 each) but activity records (`hipKernelActivity`) *all* report
`devId=7` (consistent with all physical execution landing on device 3, offset-encoded).
Host-callback `devId` itself (`hip_get_cached_device()` → `hipGetDevice()`,
roctracer_callback.c:77) is separately always 0 for every rank too — that part IS a
process-relative-vs-physical numbering artifact (each process's HIP runtime only sees
one masked device, so it calls itself "device 0" regardless of which physical GPU) and
would exist even with correct binding; but the activity records' *single* devId value
across all ranks reveals the deeper bug: binding itself is broken, not just the reported
index.

**Why this didn't surface in the single-node energy calibration**: PInsight's energy
backend (AMD-SMI) reads all of a node's physical GPU power counters directly at the
system level, bypassing `ROCR_VISIBLE_DEVICES` entirely — so seeing 4 different non-zero
per-GPU wattages in `calib/parse_energy.py` output reflects the node's real 4-GPU power
draw, NOT evidence that compute was spread across 4 GPUs by 4 different ranks. Don't
mistake energy-backend GPU enumeration for confirmation of correct compute placement.

**Fix (matches this project's own established precedent)**: `run_4rank_local.sh`
*already* doesn't trust `-g1`'s automatic binding — it computes
`ROCR_VISIBLE_DEVICES=$((FLUX_TASK_RANK % NRANKS))` itself. Applied the same pattern to
`amg2023_flux_multinode.sh`: `export ROCR_VISIBLE_DEVICES=$((FLUX_TASK_RANK %
RANKS_PER_NODE))` right before the AMG launch. Ranks are assigned in contiguous blocks
per node (confirmed via diag5: global ranks 0-3 → node 1, 4-7 → node 2, etc.), so
`FLUX_TASK_RANK % RANKS_PER_NODE` correctly gives the local-within-node index. **Not yet
re-validated with a full production run** — next run should show devId spread across the
node's 4 GPUs and ~4x lower per-GPU kernel-launch overlap (each GPU now doing 1/4 the
work, not all 16 ranks' worth... actually 4 ranks' worth funneled onto 1 GPU).

**Bug 4 fix CONFIRMED 2026-07-17 (both halves).** (a) Launcher-side:
`ROCR_VISIBLE_DEVICES=$((FLUX_TASK_RANK % RANKS_PER_NODE))` in
`amg2023_flux_multinode.sh` — verified via activity-record `devId`, now spread across
4,5,6,7 per node (~9200-9900 kernels each, was 100% devId=7 before). (b) PInsight source:
`hip_get_cached_device()` in `src/roctracer_callback.c` now adds
`hip_visible_device_offset()` (parses `ROCR_VISIBLE_DEVICES`/`HIP_VISIBLE_DEVICES`, first
comma-separated token via `atoi`, falls back to 0/plain `hipGetDevice()` if neither set —
backward compatible) on top of `hipGetDevice()`'s process-relative index. Rebuilt
`build_eval/libpinsight.so`. Verified on a real 4-node run: host-callback `devId` now
shows 0,1,2,3 per node, **exactly matching each rank's local index**
(e.g. tuolumne1039: mpirank 8/9/10/11 → devId 0/1/2/3) — fully consistent with the
launcher fix, as predicted. Left `HIP_get_device_id()` (the separate device-range trace
FILTER function used by `TRACE_PUNIT1` in trace_domain_HIP.h) untouched — out of scope,
filter range is 0-16 so behavior is unaffected either way.

**Numbering mismatch confirmed real, not reconciled**: host-callback devId (0-3, physical,
via our fix) and activity-record devId (4-7, ROCm driver-level, unmodified) use DIFFERENT
numbering for the same 4 physical GPUs. The +4 offset is now confirmed CONSISTENT across
all 4 independently-allocated nodes in one run (not per-node noise) — likely a stable
ROCm/HSA convention, but root cause not traced into ROCm internals. Not reconciled in the
peam XML (which keys "HIP Device" on host-callback devId only, per its own code comment
at line ~1100 — activity records are separately excluded from the timeline pending a
future segment/Java analysis, so this mismatch doesn't currently bite the visualization).

**Separate, unrelated finding (not a bug): "rank 0" appears once per node in
`pinsight_enter_exit:enter_pinsight` events — expected.** `enter_pinsight_func()` is a
GCC constructor (`enter_exit.c:21`, `__attribute__((constructor(200)))`) — runs at
LD_PRELOAD load time, before `main()`/`MPI_Init`, before PMPI interception has captured
the real rank, so `mpirank` is still at its zero-initialized default. Every rank's
`enter_pinsight` event necessarily shows `mpirank=0` at that point — confirmed by
matching `exit_pinsight` (same pid, fired at program end) showing the correct distinct
rank. Don't trust `enter_pinsight`'s own mpirank field for rank identification —
correlate by `pid` against a later event instead.

**Verified 2026-07-17 (`calib/rank_check.c`, `calib/rank_check_job.sh`, 2 nodes/8 ranks):
`PMI_RANK`, `FLUX_TASK_RANK`, and `PALS_RANKID` are all set by the launcher BEFORE
`MPI_Init` and all EXACTLY match `MPI_Comm_rank(MPI_COMM_WORLD)` post-init, for every
rank** (Cray MPICH + Flux + Cray PALS on Tuolumne). `OMPI_COMM_WORLD_RANK`/`MPI_RANK`/
`PMI_ID` unset (not Open MPI). Gotcha: `flux batch --output=/--error=` with RELATIVE
paths silently wrote to a location that never showed up (files never created where
expected) — always use absolute paths for these, matching the working pattern in
`amg2023_flux_multinode.sh`.

**IMPLEMENTED + VERIFIED 2026-07-17: `enter_pinsight` early-rank fix.** Added
`pinsight_early_rank_init()` in `src/enter_exit.c` (guarded `#ifdef PINSIGHT_MPI` —
`mpirank` only exists in MPI builds, declared `extern` in each
`*_lttng_ust_tracepoint.h`): checks `PMI_RANK` then `OMPI_COMM_WORLD_RANK`, `atoi()`s
whichever is set, assigns to the global `mpirank` — called at the top of
`enter_pinsight_func()` (a GCC constructor, `enter_exit.c` line ~21) before the
`enter_pinsight` tracepoint fires. Deliberately did NOT remove the later authoritative
`PMPI_Comm_rank()` capture in `pmpi_mpi.c`'s `MPI_Init`/`MPI_Init_thread` wrappers — that
call is already a one-time, cached capture (not per-event, no performance cost to
removing), and it's the only universally-correct source (works regardless of launcher;
env vars are a best-effort guess that could be wrong/absent under e.g. Slurm's native
launcher, which uses `SLURM_PROCID` instead). The two are complementary: env-var guess
for the pre-MPI_Init window where nothing else is possible, real MPI API as ground truth
once available (and it naturally overwrites the early guess). Needed `#include <stdlib.h>`
in enter_exit.c for `getenv`/`atoi`. Rebuilt `build_eval/libpinsight.so`.

**Verification (1-node/4-rank run via `amg2023_flux_multinode.sh NODES=1
RANKS_PER_NODE=4`, safest path — avoid hand-writing one-off flux-run scripts, easy to
introduce subtle quoting bugs; use the proven heredoc-generated script instead):**
`enter_pinsight` now shows `mpirank=0,1,2,3` (previously always 0 for all 4), and each
value exactly matches that same PID's later `exit_pinsight` mpirank — e.g. pid 3873017:
rank 2 in both enter and exit. Fix confirmed working correctly.

**Verification (4 nodes, 16 ranks, L=200, confirms README's own checklist)**:
inter-rank MPI genuinely captured — 76266 `MPI_Irecv` + 75638 `MPI_Isend` + 11164
`MPI_Waitall` + `MPI_Send`/`Recv`/`Allreduce`/`Barrier`/`Init`/`Finalize` per node (not
just collectives); all `mpirank` 0-15 present across the 4 node traces; ~240K HIP events +
8 energy + 8 enter_exit events per node. AMG itself: 19 iterations, FOM ~1.78e8,
consistent with the single-node calibration numbers.
