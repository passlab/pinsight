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
