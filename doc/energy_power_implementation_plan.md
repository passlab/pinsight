# Energy/Power Measurement Implementation Plan

**Date:** 2026-05-29 (verified 2026-06-10; Feature 1 implemented 2026-06-11)
**Author:** Yonghong Yan
**Status:** Feature 1 (enter/exit energy) implemented & validated on Tuolumne MI300A;
Feature 2 (power polling) and the `[Energy]`/`[Power]` config sections still TODO

> **2026-07-23 update — runtime `measure` config (env + `[Energy] measure`), via the shared
> node-policy.** Energy gains its **first runtime control** (it is compile-time-only today —
> no env, no config parser; see "Current Status"). One policy selects *which ranks* measure:
> `off | on | anyone_per_node | leader_per_node`. Two inputs, both writing a single global
> `energy_measure_policy` in `energy.{h,c}` (default `on` = today's all-ranks behavior):
> - **Env `PINSIGHT_MEASURE_ENERGY`** = `ON|OFF|ANYONE_PER_NODE|LEADER_PER_NODE` (one-time at
>   startup) — **overrides** —
> - **`[Energy] measure`** = `off|on|anyone_per_node|leader_per_node`: a **minimal new
>   `[Energy]` section handler** (the first one; the full `[Energy]`/`[Power]` struct + masks
>   below remain TODO).
>
> `anyone_per_node`/`leader_per_node` make exactly **one rank/node** measure — the **in-tool
> fix for the multi-rank multi-count caveat** (previously "use one rank per hostname" in
> analysis). `leader_per_node` = deterministic (node leader via launcher env / MPI split);
> `anyone_per_node` = arbitrary (flock on `/tmp/$USER/pinsight_energy_singleton.lock`).
> Resolved **once at startup (per-run)** — energy is not windowed (enter/exit are per-run;
> phase-gate power via `follow_mode`). Shares the `pinsight_nodepolicy_t` enum +
> `pinsight_parse_nodepolicy()` + `pinsight_node_role()` with HIP/CUDA `device_activity`, but
> energy is **not a domain**, so it does **not** use the `TRACE_NODEPOLICY` DSL. When the
> `energy_power_config_t` struct below is built, `measure` folds into it (drop the global).
> Full design + rationale: **doc/node_singleton_measurement_design.md**.

> **2026-06-11 implementation note (Feature 1).** Built on branch
> `energy-power-cleanup`. Several design points evolved during implementation — where this
> note conflicts with the older plan body below, this note wins:
>
> - **Pluggable backends, not one inline `energy.c`.** A small seam (`energy_backend.h`)
>   with a coordinator (`energy.c`) + backends: `energy_sysfs.c` (powercap + amd_energy
>   CPU), `energy_amd_smi.c` (`PINSIGHT_ENERGY_AMD_GPU`), `energy_variorum.c` (NODE stub,
>   `PINSIGHT_ENERGY_VARIORUM`). Activation policy: NODE supersedes; ≤1 CPU; all GPU.
> - **AMD-SMI is the working Tuolumne path.** On MI300A the native RAPL/amd_energy sysfs is
>   root-locked (CVE-2020-8694) and the *system* Variorum returns `UNSUPPORTED_PLATFORM`.
>   AMD-SMI (`amdsmi_get_energy_count`, ROCm 7.2.1) reads real per-APU combined CPU+GPU+HBM
>   package energy as a non-root user (~120 W idle/APU). Variorum stays a stub.
> - **Tracepoint fields are LTTng sequences (`cpu_uj`, `gpu_uj`, µJ)** — no 4-CPU/4-GPU
>   ceiling (supersedes review-note #4 and the fixed-scalar layout below). Unit unified to µJ.
> - **AMD-SMI teardown constraint.** Reading amdsmi on the main thread during DSO teardown
>   *after* the constructor's read corrupts the heap (its gpu_metrics path). So `energy_enter`
>   is read in the constructor, but **`energy_exit` is emitted from the control thread as it
>   shuts down** (inside `pinsight_control_thread_stop`, after the `exit_pinsight` marker) —
>   not the library destructor, and not `atexit` (a DSO's atexit runs during `_dl_fini`).
>   `amdsmi_shut_down()` is never called (unsafe at teardown; the OS reclaims).
> - **Config sections deferred.** Per-socket/device masks (`[Energy]`/`[Power]`) are not yet
>   parsed; for now every discovered/readable source is read. The `BitSet` reuse (review
>   note #2) still applies when that lands.

> **2026-06-10 review note.** The plan was checked against the current tree. The
> architecture stands as written. Five corrections were folded in inline (each marked
> **Correction** / note where it applies):
> 1. The `ompt_callback.c` cleanup is a **full-file sweep of ~30 sites**, not 4 blocks.
> 2. Masks should use the existing **`BitSet`** type + `bitset_parse_ranges()`, not
>    hand-rolled `uint64_t` — the range/list parser already exists.
> 3. **Config must be parsed before `pinsight_energy_init()`** for default masks to work
>    (holds today via the constructor; flagged so it stays that way).
> 4. The tracepoint layout has a hard **4-CPU / 4-GPU ceiling**; fits one MI300A node.
>    *(Superseded 2026-06-11: now LTTng sequences, no ceiling.)*
> 5. Dev order (Intel CPU first) ≠ payoff order (**AMD APU path is the real deliverable**).

---

## Current Status (2026-06-11) — read this first to continue the work

All work is on branch `energy-power-cleanup`. Implementation commits, in order:

| Commit | What |
|--------|------|
| `5abef0d` | Removed old RAPL piggy-back (rapl.c/h, ~30-site ompt_callback.c sweep, ENERGY macros) |
| `6cf7258` | Feature 1: enter/exit snapshots, dedicated `energy_pinsight_lttng_ust` provider, powercap CPU |
| `658d3d9` | Tracepoint fields → LTTng dynamic sequences (`cpu_uj`/`gpu_uj`), no socket/device ceiling |
| `509dc7e` | Pluggable backends: AMD-SMI (APU/GPU, **the working MI300A path**), amd_energy CPU, Variorum stub; energy_exit moved to control thread |

### What works today

- **`PINSIGHT_ENERGY`** (Feature 1): `energy_enter` at library constructor, `energy_exit`
  from the control thread at shutdown. Two µJ sequences per event; delta/duration gives
  total J and average W per source.
- **Backends** (`src/energy_backend.h` seam, `src/energy.c` coordinator; activation:
  NODE supersedes → ≤1 CPU → all GPU):
  - `energy_sysfs.c` — powercap(intel-rapl) + amd_energy(hwmon). Root-locked on LC
    (CVE-2020-8694) → auto-skipped with a stderr note. Value-validation still needs a
    root/group-permitted machine.
  - `energy_amd_smi.c` — `PINSIGHT_ENERGY_AMD_GPU`, links libamd_smi (ROCm). **Validated
    on Tuolumne as non-root**: 4× MI300A per-APU combined CPU+GPU+HBM package energy,
    ~120 W idle/APU, correct deltas.
  - `energy_variorum.c` — `PINSIGHT_ENERGY_VARIORUM`, placeholder stub (system Variorum
    doesn't support MI300A).
- **Validated end-to-end** on tuolumne2151: LD_PRELOAD → lttng session → babeltrace2 shows
  both events, non-zero `gpu_uj`, clean exit.

### TODO queue (dependency order)

1. `[Energy]` config section — per-platform on/off + socket/device `BitSet` masks
   (parser notes in this doc; `bitset_parse_ranges()` exists). **(2026-07-23)** A minimal
   `measure`-only `[Energy]` handler + the `PINSIGHT_MEASURE_ENERGY` env land **first** (the
   node-singleton `measure` policy, stored as a global — see the top note); the per-platform
   enables + masks + the `energy_power_config_t` struct are the remainder, into which
   `measure` later folds.
2. Feature 2 `PINSIGHT_POWER` — control-thread timed polling + `energy_sample` (§2.x);
   on/off design decided: `enabled` master switch + `follow_mode` + `pinsight_power_on/off()` API.
3. `pinsight_energy_marker(label)` — arbitrary-point snapshots; primitive for the future
   `pinsight_trace_begin/end` user-region API (layered approach agreed).
4. NVML / Level-Zero GPU backends; real Variorum backend if a supporting build appears.
5. Analysis-side: per-source J/W summary, Intel wraparound correction
   (`max_energy_range_uj` is already captured at init).

### Hard-won constraints — do not regress

- **AMD-SMI teardown**: never call amdsmi (read or `amdsmi_shut_down()`) on the main
  thread during DSO teardown after the constructor has read it — heap corruption in its
  gpu_metrics path. `atexit` does NOT help (DSO atexit runs in `_dl_fini`). The exit read
  must stay on a **live thread** → it lives at the end of `pinsight_control_loop()`.
- **Fresh CMake on Tuolumne picks the system LTTng 2.8** and fails; reconfigure the
  existing `build_omp/`/`build_hip/` (they carry the ~/local LTTng 2.13 paths).

### Running an application evaluation on Tuolumne (current procedure)

```bash
# 1. Build (once): reconfigure the existing build dir, do NOT cmake from scratch
cd build_omp   # or build_hip for HIP-traced apps
cmake -DPINSIGHT_ENERGY_AMD_GPU=TRUE -DROCM_PATH=/opt/rocm-7.2.1 . && make

# 2. Environment
export PATH=$HOME/local/bin:$PATH
export LD_LIBRARY_PATH=$HOME/local/lib:/opt/rocm-7.2.1/lib:$LD_LIBRARY_PATH

# 3. Trace session
lttng create esess --output=$PWD/energy_trace
lttng enable-event -u 'energy_pinsight_lttng_ust:*'   # add other providers as needed
lttng start
LD_PRELOAD=/path/to/libpinsight.so <app> <args>
lttng stop && lttng destroy

# 4. Read out (per source i): J = (exit.gpu_uj[i] - enter.gpu_uj[i]) / 1e6
#                             W = J / ((exit.ts - enter.ts) in seconds)
babeltrace2 energy_trace | grep energy_
```

Evaluation caveats:
- **MI300A semantics**: `gpu_uj[i]` is APU *i*'s **combined CPU+GPU+HBM package** energy —
  there is no CPU/GPU split on this hardware. `cpu_uj` is empty (RAPL root-locked).
- **Multi-rank nodes**: every rank's libpinsight reads the same 4 node-local APU counters.
  For node energy, use **one rank per hostname** (events carry `hostname` + `pid`);
  summing across ranks on the same node multiple-counts. **(2026-07-23) In-tool fix:** set
  `PINSIGHT_MEASURE_ENERGY=LEADER_PER_NODE` (env) or `[Energy] measure = leader_per_node`
  (config) so only one rank/node emits energy — no analysis-side dedup needed (see the
  2026-07-23 note near the top).
- **Baseline**: idle is ~120 W/APU — subtract an idle-run baseline if you want
  application-attributable energy rather than wall-clock node energy.
- **Short runs**: counters update at ~ms scale; runs ≪ 1 s give noisy W estimates.

---

## Two Features, Two CMake Flags

Energy and power measurement are implemented as two independent, layered features:

| Feature | CMake flag | What it measures | Overhead |
|---------|------------|-----------------|---------|
| **Energy** | `PINSIGHT_ENERGY` | Total application energy (joules) — two counter reads, at library enter and exit | Zero runtime overhead |
| **Power** | `PINSIGHT_POWER` | Power over time (watts) — periodic counter reads by the control thread | Zero on application hot path |

`PINSIGHT_POWER=TRUE` implies `PINSIGHT_ENERGY=TRUE` — periodic polling reuses the same
`energy.c` read functions. CMake enforces this automatically:

```cmake
if(PINSIGHT_POWER AND NOT PINSIGHT_ENERGY)
    set(PINSIGHT_ENERGY TRUE)
endif()
```

The naming maps directly to physics: **Energy** (E) is a scalar total from two counter
readings. **Power** (P = dE/dt) requires a time series.

---

## Before You Implement — Files to Read First

A future implementation session must read these files before writing any code.
The plan references them but does not reproduce their content.

### 1. `src/trace_config_parse.c` — understand the config parser pattern

The `[Energy]` and `[Power]` sections follow the same INI-style parser already used
for `[OpenMP.global]`, `[Lexgion]`, etc. Read `trace_config_parse.c` to understand:
- How section headers (`[SectionName]`) are detected and dispatched
- How key=value pairs are parsed within a section
- How `on`/`off` boolean values are read
- How existing range syntax (`range = 0-3`) is parsed for punits — the same function
  is reused for `intel_cpu_sockets = 0,1` and `nvidia_gpu_devices = 2`
- Where to register the new `[Energy]` and `[Power]` section handlers

### 2. `src/trace_config.h` — understand where the new struct fits

Read `trace_config.h` to see the existing config structures (`lexgion_trace_config_t`,
`domain_trace_config_t`, etc.) and find the right place to add `energy_power_config_t`.
Also check whether `energy_power_config` should be declared `extern` alongside the
existing global config variables.

### 3. `src/ompt_lttng_ust_tracepoint.h` lines 62–82 — the macros to delete

The three macros that must be removed are:
```c
#define ENERGY_LTTNG_UST_TP_ARGS       /* ~line 63 */
#define ENERGY_LTTNG_UST_TP_FIELDS     /* ~line 70 */
#define ENERGY_LTTNG_UST_TRACEPOINT_CALL_ARGS  /* ~line 78 */
```
And their empty `#else` counterparts at ~lines 80–82. Also remove the `package_energy`
global variable declaration referenced in the comment at ~line 76.

### 4. `src/ompt_callback.c` — the energy sweep (NOT four blocks — ~30 sites)

**Correction (verified 2026-06-10):** energy is not confined to four callbacks. It is
piggy-backed onto **nearly every OMPT tracepoint** in this 1800-line file. As of this
writing the file contains:
- **~33** `#ifdef PINSIGHT_ENERGY` blocks (`grep -c` confirms)
- **30** `rapl_sysfs_read_packages(package_energy)` calls — one before most tracepoints
- **32** `ENERGY_LTTNG_UST_TRACEPOINT_CALL_ARGS` usages appended to tracepoint call sites

Each callback does the same two things: `rapl_sysfs_read_packages(package_energy)` to
refresh the global array, then passes `ENERGY_LTTNG_UST_TRACEPOINT_CALL_ARGS` (which
expands to `,package_energy[0],...,package_energy[3]`) as the trailing tracepoint args.

So the cleanup is a **full-file sweep**, not a 4-block edit:
- Remove every `#ifdef PINSIGHT_ENERGY ... rapl_sysfs_read_packages(...) ... #endif` block
- Remove every `ENERGY_LTTNG_UST_TRACEPOINT_CALL_ARGS` token from tracepoint call sites
  (these become hard compile errors once the macro is deleted — that is the safety net)
- Remove the `#include "rapl.h"` + `static long long package_energy[MAX_PACKAGES];`
  declaration block near the top (~line 22)

Mechanical but large. Do it as one isolated commit and confirm a clean build before/after.
The fact that removing the macros breaks compilation at every leftover site is a feature —
lean on the compiler to find any missed sites.

### 5. `src/enter_exit.c` — exact wiring points

Read `enter_exit.c` to confirm the constructor/destructor structure before adding
energy calls. The empty `#ifdef PINSIGHT_ENERGY` stub at ~line 53 is the placeholder —
replace it with the full init/read/tracepoint sequence shown in section 1.5.

### 6. `src/pinsight_control_thread.c` — the existing loop before extending it

Read the `pinsight_control_loop()` function to understand the `sem_wait` /
`sem_timedwait` structure before adding the power polling branch. Pay attention to:
- The `control_shutdown` flag and how the loop exits
- The existing `sem_timedwait` path used for INTROSPECT (the power polling reuses
  this pattern but as the primary loop mode, not a special case)
- Where `control_apply_all_modes()` is called — the energy sample emission sits
  in the timeout branch, separate from the signal-woken branch

---

## Background and Design Decisions

### What we measure

RAPL (Intel), amd_energy (AMD), and GPU energy interfaces expose **monotonically increasing
hardware accumulators** — not instantaneous power. The design reads these counters at two
points and diffs them:

```
total_energy  = exit_counter  - enter_counter    (µJ per socket / mJ per GPU device)
average_power = total_energy  / (t_exit - t_enter)   (Watts)
```

LTTng timestamps already on every tracepoint provide `t_enter` and `t_exit` for free.

### Why not read inside domain callbacks (OpenMP, HIP, MPI, …)

Hardware counters update at ~1 ms resolution. Short regions (<1 ms) would read identical
values at begin and end — delta = 0, no information, pure overhead. The one-thread-reads-
all-sockets pattern is already correct at enter/exit; spreading reads across OMPT/HIP
callbacks adds complexity with no gain.

### Thread ownership

CPU energy counters are **global hardware state** — any thread can read any socket's
counter. The main thread (which runs `enter_pinsight_func` / `exit_pinsight_func`) is the
natural single reader for `PINSIGHT_ENERGY`. For `PINSIGHT_POWER`, the dedicated PInsight
control thread takes over periodic polling, keeping all energy code off the application's
hot path.

### Energy/Power is NOT a domain

The domain framework (OpenMP, MPI, CUDA, HIP, Python) handles **intercepted runtime
events** — callbacks pushed by the runtime at specific code locations. Its value comes from
lexgions, per-lexgion rate control, punits, and the 4-mode hierarchy. None of these
concepts apply to energy/power:

| Domain concept | Applies to energy/power? | Reason |
|----------------|--------------------------|--------|
| Lexgions (code locations) | No | RAPL counters are global hardware state, not tied to any source location |
| Rate control (max_num_traces, tracing_rate) | No | Sampling is time-driven, not code-driven |
| Named lexgion config | No | No code identity for a hardware counter |
| Punits (thread, rank, stream) | No | Energy is per-socket or per-device, not per execution unit |
| 4-mode hierarchy (OFF/STANDBY/MONITORING/TRACING) | No | STANDBY and MONITORING have no meaningful interpretation for polling |
| Callback-driven events | No | Energy is pull (polled), not push (callback) |

Energy/Power is a **measurement service** with its own CMake flags, its own tracepoint
provider (`energy_pinsight_lttng_ust`), and its own `[Energy]` / `[Power]` config
sections. Runtime on/off control per platform type is sufficient — no 4-mode state machine
is needed.

### Remove the old RAPL implementation

`src/rapl.c` / `src/rapl.h` use `fopen`/`fscanf`/`fclose` per read and are Intel-only.
They are replaced by the new `src/energy.c` / `src/energy.h`. The `ENERGY_LTTNG_UST_TP_*`
macros in `ompt_lttng_ust_tracepoint.h` that piggy-back energy onto OMPT events are also
removed — energy gets its own dedicated tracepoints.

---

## Three-Layer Control

Control is independent at three layers. Crucially, `[Energy]` and `[Power]` have
**independent** socket/device selection — enabling all sockets for energy snapshots does
not force the same set for power polling, and vice versa:

```
Layer 1 — CMake (compile-time)     Layer 2 — [Energy] config              Layer 3 — [Power] config
────────────────────────────────   ────────────────────────────────────    ──────────────────────────────────────
PINSIGHT_ENERGY                →   intel_cpu  = on/off                    intel_cpu_poll_interval_ms = 10
                                   intel_cpu_sockets = 0,1  (enter/exit)  intel_cpu_sockets = 0  (poll only)
PINSIGHT_ENERGY                →   amd_cpu    = on/off
                                   amd_cpu_sockets = 0,1
PINSIGHT_ENERGY                →   arm_cpu    = on/off
                                   arm_cpu_sockets = all
PINSIGHT_ENERGY_NVIDIA         →   nvidia_gpu = on/off                    nvidia_gpu_poll_interval_ms = 100
                                   nvidia_gpu_devices = 0,1,2             nvidia_gpu_devices = 2  (poll only)
PINSIGHT_ENERGY_AMD_GPU        →   amd_gpu    = off  (no snapshot)        amd_gpu_poll_interval_ms = 200
                                                                           amd_gpu_devices = 1  (poll anyway)
PINSIGHT_ENERGY_INTEL_GPU      →   intel_gpu  = on/off
                                   intel_gpu_devices = all
```

A platform with `poll_interval_ms > 0` in `[Power]` **is polled even if disabled in
`[Energy]`** — the two features are fully independent. Runtime config can only activate
what is compiled in; specifying `nvidia_gpu_poll_interval_ms = 100` when
`PINSIGHT_ENERGY_NVIDIA` is not set is silently ignored.

---

## Config Design

`[Energy]` and `[Power]` are fully independent sections. Each carries its own
per-platform enable and socket/device selection. `[Power]` inherits `[Energy]`'s
socket/device selection as its **default** when the key is absent — so you only need
to write what differs from the snapshot selection.

### `[Energy]` section — per-platform enable + socket/device selection for enter/exit reads

```ini
[Energy]
# WHICH RANKS measure (node-singleton policy; default on = every rank). Env
# PINSIGHT_MEASURE_ENERGY overrides this. Resolved once at startup (per-run).
# See the 2026-07-23 note + doc/node_singleton_measurement_design.md.
measure             = leader_per_node   # off | on | anyone_per_node | leader_per_node

# Per-platform enable (default: on for platforms detected at runtime)
# Per-platform socket/device selection (default: all when key absent)
intel_cpu           = on
intel_cpu_sockets   = 0,1       # sockets to read at enter/exit

amd_cpu             = on
amd_cpu_sockets     = 0,1

arm_cpu             = off       # disabled even if hardware present

nvidia_gpu          = on        # requires PINSIGHT_ENERGY_NVIDIA at compile time
nvidia_gpu_devices  = 0,1,2     # GPU devices to read at enter/exit

amd_gpu             = off       # no enter/exit snapshot for AMD GPU
                                # (can still be polled by [Power] independently)

intel_gpu           = off       # requires PINSIGHT_ENERGY_INTEL_GPU
```

### `[Power]` section — per-platform poll intervals + independent socket/device selection

Two granularities of on/off:
- **Per-platform:** `*_poll_interval_ms = 0` disables polling for that one platform — the
  interval is the enable/disable control, no separate per-platform flag needed.
- **Whole-feature:** `enabled = off` disables *all* polling at once (forces `tick_ms = 0`,
  control thread reverts to the blocking `sem_wait` path with zero overhead) regardless of
  the per-platform intervals. This is the natural switch to flip live (see "Runtime on/off
  control" below) without having to zero six interval keys. **Default when the key is
  absent: `on`** — so an existing `[Power]` section with only intervals keeps working
  (the parser must initialize `power_enabled = 1`).

Socket/device keys are **optional**: if absent, the value from `[Energy]` is inherited; if
present, it applies to polling only.

```ini
[Power]
enabled      = on        # whole-feature master switch; off → no polling at all
follow_mode  = off       # on → poll only while some domain is MONITORING/TRACING
                         #      (phase-gated power; reuses region-of-interest config)

# Per-platform poll interval in ms; 0 = disabled for that platform
# Per-platform socket/device selection (optional — inherits from [Energy] if absent)
intel_cpu_poll_interval_ms   = 10
intel_cpu_sockets            = 0        # poll only socket 0, even though [Energy] reads 0,1

amd_cpu_poll_interval_ms     = 10
# amd_cpu_sockets absent → inherits 0,1 from [Energy]

arm_cpu_poll_interval_ms     = 0        # disabled

nvidia_gpu_poll_interval_ms  = 100
nvidia_gpu_devices           = 2        # poll only GPU 2, even though [Energy] reads 0,1,2

amd_gpu_poll_interval_ms     = 200      # poll AMD GPU for power even though
amd_gpu_devices              = 1        # [Energy] has amd_gpu=off

intel_gpu_poll_interval_ms   = 0        # disabled
```

### Index syntax for sockets and devices

Reuses the range notation already in `trace_config_parse.c` for punits, extended with
comma-separated lists:

| Syntax | Meaning |
|--------|---------|
| `0` | single socket or device |
| `0,2` | specific indices |
| `0-3` | inclusive range |
| `0,2-4,6` | mixed list and range |
| `all` | all available (default when key is absent) |

### Config examples

**CPU energy only, no GPU** (pure OpenMP job):
```ini
[Energy]
nvidia_gpu = off
amd_gpu    = off
intel_gpu  = off
```

**GPU energy only, no CPU** (pure GPU application):
```ini
[Energy]
intel_cpu = off
amd_cpu   = off
arm_cpu   = off
```

**Snapshot all sockets and all GPUs, but poll only socket 0 and GPU 2 for power**:
```ini
[Energy]
intel_cpu_sockets  = 0,1
nvidia_gpu_devices = 0,1,2

[Power]
intel_cpu_poll_interval_ms  = 10
intel_cpu_sockets           = 0    # narrower than [Energy]
nvidia_gpu_poll_interval_ms = 100
nvidia_gpu_devices          = 2    # narrower than [Energy]
```

**No AMD GPU energy snapshot, but still poll it for power profile**:
```ini
[Energy]
amd_gpu = off                      # skip at enter/exit

[Power]
amd_gpu_poll_interval_ms = 200     # poll anyway — independent of [Energy]
amd_gpu_devices          = 1
```

**Enter/exit for everything, power profile for nothing** (Feature 1 only, no Feature 2):
```ini
# Compile with -DPINSIGHT_ENERGY=TRUE -DPINSIGHT_POWER=FALSE
# No [Power] section needed
```

---

## Runtime Config Structure

Add to `src/trace_config.h`:

```c
#define MAX_ENERGY_PACKAGES  16
#define MAX_ENERGY_GPU_DEVS  16

typedef struct {
    /* PINSIGHT_ENERGY — enter/exit snapshots */
    int      enabled;              /* 0 = off, 1 = on */
    uint64_t socket_mask;          /* bit N = socket N; populated from [Energy] */

    /* PINSIGHT_POWER — periodic polling (independent of above) */
    int      poll_interval_ms;     /* 0 = disabled; >0 = polling active */
    uint64_t power_socket_mask;    /* bit N = socket N; defaults to socket_mask,
                                      overridden by [Power] intel_cpu_sockets */
} cpu_energy_config_t;

typedef struct {
    /* PINSIGHT_ENERGY */
    int      enabled;
    uint64_t device_mask;          /* populated from [Energy] */

    /* PINSIGHT_POWER */
    int      poll_interval_ms;
    uint64_t power_device_mask;    /* defaults to device_mask,
                                      overridden by [Power] *_devices */
} gpu_energy_config_t;

typedef struct {
    cpu_energy_config_t intel_cpu;
    cpu_energy_config_t amd_cpu;
    cpu_energy_config_t arm_cpu;
    gpu_energy_config_t nvidia_gpu;
    gpu_energy_config_t amd_gpu;
    gpu_energy_config_t intel_gpu;

    /* PINSIGHT_POWER — whole-feature on/off control ([Power] section) */
    int      power_enabled;        /* [Power] enabled; 0 forces tick_ms=0 (no polling) */
    int      follow_mode;          /* [Power] follow_mode; 1 = poll only when a domain
                                      is MONITORING/TRACING (phase-gated power) */

    int      tick_ms;              /* GCD of all active poll_interval_ms; 0 = no polling.
                                      Forced to 0 when power_enabled == 0. */
} energy_power_config_t;

extern energy_power_config_t energy_power_config;
```

> **Correction (verified 2026-06-10):** the `uint64_t socket_mask` / `device_mask` /
> `power_*_mask` fields above should be **`BitSet`** to match the existing config code
> (`bitset_parse_ranges`, punit masks). The `uint64_t` form is shown only because it makes
> the bit-test logic in later snippets easy to read. Implement with `BitSet` + its
> accessors. See "parse_index_list" under Supplementary Details.

> **Ordering invariant:** `pinsight_energy_init()` (wired into `enter_pinsight_func`) reads
> the discovered socket/device counts and applies the "default = all when mask unset"
> fallback (see "Default socket/device mask"). This requires the config file to have been
> **fully parsed and `energy_power_config_finalize()` already called** before enter runs.
> The library constructor parses config before `enter_pinsight_func`, so this holds — but
> if init is ever moved, preserve this ordering or the masks will be empty.

Config parser populates the struct in two passes:

1. **Parse `[Energy]`** — set `enabled`, `socket_mask` / `device_mask`; copy to
   `power_socket_mask` / `power_device_mask` as defaults.
2. **Parse `[Power]`** — set `poll_interval_ms`; override `power_socket_mask` /
   `power_device_mask` where the key is explicitly present.
3. **Compute `tick_ms`** = GCD of all non-zero `poll_interval_ms` values.

A platform with `poll_interval_ms > 0` is polled regardless of `enabled` — the two
features are independent. Enter/exit read loop uses `socket_mask`; poll loop uses
`power_socket_mask`:

```c
/* Enter/exit read — uses socket_mask from [Energy] */
for (int s = 0; s < num_sockets; s++) {
    if (cfg->intel_cpu.socket_mask & (1ULL << s))
        pinsight_energy_read_intel_socket(s, &uj[s]);
}

/* Poll loop — uses power_socket_mask from [Power] (may differ) */
for (int s = 0; s < num_sockets; s++) {
    if (cfg->intel_cpu.power_socket_mask & (1ULL << s))
        pinsight_energy_read_intel_socket(s, &uj[s]);
}
```

`tick_ms` drives the single control thread timer; each platform reads at its own sub-rate:

```c
tick_ms = gcd_of_active_intervals();  /* skip zeros */

tick++;
if (cfg->intel_cpu.poll_interval_ms > 0 &&
    tick % (cfg->intel_cpu.poll_interval_ms / tick_ms) == 0)
    pinsight_energy_read_intel_cpu(&e);

if (cfg->nvidia_gpu.poll_interval_ms > 0 &&
    tick % (cfg->nvidia_gpu.poll_interval_ms / tick_ms) == 0)
    pinsight_energy_read_nvidia_gpu(&e);
```

---

## Feature 1: `PINSIGHT_ENERGY` — Enter/Exit Energy Snapshots

**Goal:** Total application energy and average power with zero runtime overhead.
**Reads:** Exactly two — one at `enter_pinsight_func()`, one at `exit_pinsight_func()`.

### 1.1 CMake options

```cmake
option(PINSIGHT_ENERGY           "Total application energy at enter/exit"      FALSE)
option(PINSIGHT_ENERGY_NVIDIA    "NVIDIA GPU energy via NVML"                  FALSE)
option(PINSIGHT_ENERGY_AMD_GPU   "AMD GPU energy via AMD-SMI"                  FALSE)
option(PINSIGHT_ENERGY_INTEL_GPU "Intel GPU energy via Level-Zero"             FALSE)
option(PINSIGHT_POWER            "Power-over-time profiling via control thread" FALSE)

# PINSIGHT_POWER depends on PINSIGHT_ENERGY
if(PINSIGHT_POWER AND NOT PINSIGHT_ENERGY)
    set(PINSIGHT_ENERGY TRUE)
endif()
```

CPU energy (Intel / AMD / ARM) is auto-detected via sysfs — no separate CMake flag and
no extra library dependency. When `PINSIGHT_ENERGY=TRUE`:
- Add `src/energy.c` to `SOURCE_FILES`
- Find and link `libnvidia-ml` if `PINSIGHT_ENERGY_NVIDIA=TRUE`
- Find and link `libamd_smi` if `PINSIGHT_ENERGY_AMD_GPU=TRUE`
- Find and link `libze_loader` if `PINSIGHT_ENERGY_INTEL_GPU=TRUE`
- Remove `src/rapl.c` / `src/rapl.h` from `SOURCE_FILES`

### 1.2 New file: `src/energy.h`

```c
#ifndef ENERGY_H
#define ENERGY_H

#define MAX_ENERGY_PACKAGES  16
#define MAX_ENERGY_GPU_DEVS  16

typedef struct {
    int      num_cpu_sockets;
    uint64_t cpu_energy_uj[MAX_ENERGY_PACKAGES];  /* µJ per socket; 0 if not measured */
    int      num_gpu_devices;
    uint64_t gpu_energy_mj[MAX_ENERGY_GPU_DEVS];  /* mJ per device; 0 if not measured */
} pinsight_energy_t;

/* Called once at library constructor — discovers sysfs paths, inits GPU libs */
void pinsight_energy_init(void);

/* Called once at library destructor */
void pinsight_energy_fini(void);

/* Read CPU sockets into e->cpu_energy_uj[] using socket_mask ([Energy] selection) */
void pinsight_energy_read_cpu(pinsight_energy_t *e);

/* Read GPU devices into e->gpu_energy_mj[] using device_mask ([Energy] selection) */
void pinsight_energy_read_gpu(pinsight_energy_t *e);

/* Read both CPU and GPU — for enter/exit use (socket_mask / device_mask) */
void pinsight_energy_read(pinsight_energy_t *e);

/* Poll variants — use power_socket_mask / power_device_mask ([Power] selection) */
void pinsight_energy_poll_cpu(pinsight_energy_t *e);
void pinsight_energy_poll_gpu(pinsight_energy_t *e);

#endif
```

### 1.3 New file: `src/energy.c`

#### CPU energy — sysfs discovery (no external library)

**Intel** — powercap interface (kernel ≥ 3.13, Intel Sandy Bridge+):
```
/sys/class/powercap/intel-rapl/intel-rapl:<N>/energy_uj
```
Discovery: iterate N = 0, 1, … until the path does not exist.
Unit: µJ as a 64-bit decimal integer.

**AMD EPYC** — hwmon via `amd_energy` driver (kernel ≥ 5.13 for Zen3):
```
/sys/class/hwmon/hwmon<N>/name           → match "amd_energy"
/sys/class/hwmon/hwmon<N>/energy<M>_input → µJ, 64-bit accumulator, no wraparound
```
Discovery: scan hwmon nodes, match `name == "amd_energy"`, enumerate `energy*_input`.
The `amd_energy` driver accumulates the 32-bit MSR in the kernel — safe for long runs.

**ARM** — hwmon best-effort: same scan, match any hwmon exposing `energy*_input`.
Platform-dependent; skip silently if not found.

Implementation pattern (applies to all CPU types):
```c
/* At init: discover paths into static arrays, respecting socket_mask */
static char cpu_sysfs_paths[MAX_ENERGY_PACKAGES][256];
static int  num_discovered_sockets = 0;

/* At read: fopen/fscanf/fclose per enabled socket — called only twice per run */
void pinsight_energy_read_cpu(pinsight_energy_t *e) {
    e->num_cpu_sockets = num_discovered_sockets;
    for (int s = 0; s < num_discovered_sockets; s++) {
        if (!(energy_power_config.intel_cpu.socket_mask & (1ULL << s))) continue;
        FILE *f = fopen(cpu_sysfs_paths[s], "r");
        if (f) { fscanf(f, "%"SCNu64, &e->cpu_energy_uj[s]); fclose(f); }
    }
}
```

Since this is called exactly twice per run for `PINSIGHT_ENERGY`, `fopen`/`fscanf`/
`fclose` overhead (~5–20 µs per socket) is completely irrelevant. For `PINSIGHT_POWER`
polling, open persistent fds at init and use `read()` for ~100–300 ns per socket.

#### GPU energy — native libraries (optional)

**NVIDIA — NVML** (`PINSIGHT_ENERGY_NVIDIA`):
```c
#include <nvml.h>
/* Init: */ nvmlInit_v2(); nvmlDeviceGetCount(&n); for(i) nvmlDeviceGetHandleByIndex(i,&h[i]);
/* Enter/exit read: */ uint64_t mask = cfg->nvidia_gpu.device_mask;
                       for(i) { if(mask & 1ULL<<i) nvmlDeviceGetTotalEnergyConsumption(h[i],&mj[i]); }
/* Poll read:       */ uint64_t pmask = cfg->nvidia_gpu.power_device_mask;
                       for(i) { if(pmask & 1ULL<<i) nvmlDeviceGetTotalEnergyConsumption(h[i],&mj[i]); }
/* Fini: */ nvmlShutdown();
```
`GetTotalEnergyConsumption` is a monotonically increasing millijoule counter — identical
enter/exit delta semantics as RAPL.

**AMD GPU — AMD-SMI** (`PINSIGHT_ENERGY_AMD_GPU`):
```c
#include <amd_smi/amdsmi.h>
/* Init: */ amdsmi_init(AMDSMI_INIT_AMD_GPUS); amdsmi_get_processor_handles(...);
/* Enter/exit read: */ uint64_t mask = cfg->amd_gpu.device_mask; uint64_t res, ts;
                       for(i) { if(mask & 1ULL<<i) amdsmi_get_energy_count(h[i],&mj[i],&res,&ts); }
/* Poll read:       */ uint64_t pmask = cfg->amd_gpu.power_device_mask;
                       for(i) { if(pmask & 1ULL<<i) amdsmi_get_energy_count(h[i],&mj[i],&res,&ts); }
/* Fini: */ amdsmi_shut_down();
```
**MI300A note:** `amdsmi_get_energy_count` returns combined CPU+GPU+HBM package energy —
hardware does not expose per-component breakdown. Tracepoint field should be labeled
`apu_package_energy_mj`.

**Intel GPU — Level-Zero** (`PINSIGHT_ENERGY_INTEL_GPU`):
```c
#include <level_zero/zes_api.h>
/* Enter/exit read: */ uint64_t mask = cfg->intel_gpu.device_mask;
                       zes_power_energy_counter_t ctr;
                       for(i) { if(mask & 1ULL<<i) zesPowerGetEnergyCounter(ph[i],&ctr); uj[i]=ctr.energy; }
/* Poll read:       */ uint64_t pmask = cfg->intel_gpu.power_device_mask;
                       for(i) { if(pmask & 1ULL<<i) zesPowerGetEnergyCounter(ph[i],&ctr); uj[i]=ctr.energy; }
```

### 1.4 New LTTng tracepoint provider: `energy_pinsight_lttng_ust`

New file: `src/energy_lttng_ust_tracepoint.h`. Separate provider — decoupled from OpenMP,
MPI, CUDA, HIP, and Python providers. Can be enabled/disabled in LTTng independently.

Three tracepoints share the same field layout. **Implemented (2026-06-10) with LTTng
dynamic-length sequences, not fixed cpuN/gpuN scalars** — this removes any ceiling on
socket/device count:

```c
/* Fields carried by energy_enter, energy_exit, and energy_sample.
 * TP_ARGS pass (count, array_ptr) pairs:
 *   unsigned int num_cpu, uint64_t *cpu_uj,
 *   unsigned int num_gpu, uint64_t *gpu_mj,
 *   uint64_t seq                                              */
LTTNG_UST_TP_FIELDS(
    COMMON_LTTNG_UST_TP_FIELDS_GLOBAL          /* hostname, pid (+ LTTng timestamp) */
    /* per-CPU-socket microjoules; length = num discovered sockets, position = socket index */
    lttng_ust_field_sequence(uint64_t, cpu_uj, cpu_uj, unsigned int, num_cpu)
    /* per-GPU-device millijoules; length = num discovered devices, position = device index */
    lttng_ust_field_sequence(uint64_t, gpu_mj, gpu_mj, unsigned int, num_gpu)
    /* 0 in energy_enter/exit; monotonic counter for energy_sample (PINSIGHT_POWER) */
    lttng_ust_field_integer(uint64_t, seq, seq)
)
```

LTTng emits the length automatically as a hidden field (`_cpu_uj_length` / `_gpu_mj_length`),
so analysis reads `cpu_uj[s]` for socket `s` over `range(_cpu_uj_length)`. An element is 0
when that index was not measured (unselected or unreadable). `seq` orders samples and
detects missed ticks.

> **No per-node ceiling.** Sequences scale to any socket/device count — an 8-socket NUMA
> node or 8-GPU node just emits a longer sequence, no code change. On MI300A the APU
> reports **combined CPU+GPU+HBM package energy** via AMD-SMI (one number per APU); a
> 4-APU node emits `_gpu_mj_length = 4`. Label that value `apu_package_energy_mj` in the
> analysis layer; on MI300A the `cpu_uj` sysfs sequence may be empty/redundant.

### 1.5 Wire into `enter_exit.c`

**As implemented (2026-06-10):** the tracepoint emission lives in `energy.c`
(`pinsight_energy_snapshot_enter/exit()`), so the dedicated provider is defined in a
single translation unit and `enter_exit.c` never touches it — cleaner than emitting the
tracepoint inline, and it keeps the sequence-arg marshalling in one place.

```c
void enter_pinsight_func() {
    pid = getpid(); gethostname(hostname, 48);
#ifdef PINSIGHT_ENERGY
    /* Baseline energy snapshot precedes the enter_pinsight app-start marker. */
    pinsight_energy_init();
    pinsight_energy_snapshot_enter();   /* reads + emits energy_enter (cpu_uj/gpu_mj seqs) */
#endif
    lttng_ust_tracepoint(pinsight_enter_exit_lttng_ust, enter_pinsight);
    initial_setup_trace_config();
#ifdef PINSIGHT_CUDA
    LTTNG_CUPTI_Init();
#endif
#ifdef PINSIGHT_HIP
    LTTNG_ROCTRACER_Init();
#endif
    pinsight_control_thread_start();
    pinsight_install_signal_handler();
}

void exit_pinsight_func() {
    lttng_ust_tracepoint(pinsight_enter_exit_lttng_ust, exit_pinsight);
#ifdef PINSIGHT_CUDA
    LTTNG_CUPTI_Fini();
#endif
#ifdef PINSIGHT_HIP
    LTTNG_ROCTRACER_Fini();
#endif
    pinsight_control_thread_stop();
#ifdef PINSIGHT_ENERGY
    /* Final energy snapshot follows the exit_pinsight app-end marker. */
    pinsight_energy_snapshot_exit();    /* reads + emits energy_exit */
    pinsight_energy_fini();
#endif
}

/* in energy.c — the provider is DEFINEd here and nowhere else:
 *   #define LTTNG_UST_TRACEPOINT_CREATE_PROBES
 *   #define LTTNG_UST_TRACEPOINT_DEFINE
 *   #include "energy_lttng_ust_tracepoint.h"
 * void pinsight_energy_snapshot_enter(void) {
 *     pinsight_energy_t e; pinsight_energy_read(&e);
 *     lttng_ust_tracepoint(energy_pinsight_lttng_ust, energy_enter,
 *         (unsigned int)e.num_cpu_sockets, e.cpu_energy_uj,
 *         (unsigned int)e.num_gpu_devices, e.gpu_energy_mj, (uint64_t)0);
 * }                                                                          */
```

`energy_enter` fires before `enter_pinsight` (baseline precedes app-start marker).
`energy_exit` fires after `exit_pinsight` (app-end marker precedes final snapshot).
The energy delta slightly over-counts PInsight teardown overhead — negligible.

### 1.6 Clean up old energy code

- Delete `src/rapl.c` and `src/rapl.h`
- Remove `ENERGY_LTTNG_UST_TP_ARGS`, `ENERGY_LTTNG_UST_TP_FIELDS`,
  `ENERGY_LTTNG_UST_TRACEPOINT_CALL_ARGS` macros (and their empty `#else` counterparts)
  from `src/ompt_lttng_ust_tracepoint.h` (~lines 62–84). These macros also appear in the
  `LTTNG_UST_TRACEPOINT_EVENT_*` field/arg definitions further down the file — remove
  every `ENERGY_LTTNG_UST_TP_ARGS` / `ENERGY_LTTNG_UST_TP_FIELDS` reference there too.
- Sweep `src/ompt_callback.c`: remove all ~33 `#ifdef PINSIGHT_ENERGY` blocks, all 30
  `rapl_sysfs_read_packages()` calls, and all 32 `ENERGY_LTTNG_UST_TRACEPOINT_CALL_ARGS`
  tokens (see "Files to Read First" §4 — this is a full-file sweep, not a 4-block edit)
- Remove the `#include "rapl.h"` and `static long long package_energy[MAX_PACKAGES];`
  declaration (~line 22 of `ompt_callback.c`); check `ompt_callback.h` too
- Recommended sequence: delete the three macros from `ompt_lttng_ust_tracepoint.h` first,
  then let the compiler enumerate every leftover `ENERGY_LTTNG_UST_TRACEPOINT_CALL_ARGS`
  site as a build error and clean each one.

### 1.7 Analysis — what `PINSIGHT_ENERGY` gives you

```python
enter = trace.find_event("energy_pinsight_lttng_ust:energy_enter")
exit  = trace.find_event("energy_pinsight_lttng_ust:energy_exit")
duration_s = (exit.timestamp - enter.timestamp) / 1e9

# cpu_uj / gpu_mj are sequences; iterate over their length (no fixed 0..3 loop)
for s in range(len(enter["cpu_uj"])):
    delta_uj = exit["cpu_uj"][s] - enter["cpu_uj"][s]
    if delta_uj > 0:
        print(f"CPU socket {s}: {delta_uj/1e6:.3f} J, avg {delta_uj/1e6/duration_s:.2f} W")

for d in range(len(enter["gpu_mj"])):
    delta_mj = exit["gpu_mj"][d] - enter["gpu_mj"][d]
    if delta_mj > 0:
        print(f"GPU device {d}: {delta_mj/1e3:.3f} J, avg {delta_mj/1e3/duration_s:.2f} W")
```

### 1.8 `PINSIGHT_ENERGY` file change summary

| Action | File |
|--------|------|
| **New** | `src/energy.h` |
| **New** | `src/energy.c` |
| **New** | `src/energy_lttng_ust_tracepoint.h` |
| **Modified** | `src/enter_exit.c` — wire init/read/fini + tracepoint calls |
| **Modified** | `CMakeLists.txt` — add all energy/power options, remove rapl, add GPU lib finds |
| **Modified** | `src/trace_config.h` — add `energy_power_config_t` |
| **Modified** | `src/trace_config_parse.c` — parse `[Energy]` section |
| **Modified** | `src/ompt_lttng_ust_tracepoint.h` — remove `ENERGY_LTTNG_*` macros |
| **Modified** | `src/ompt_callback.c` — remove all `#ifdef PINSIGHT_ENERGY` blocks |
| **Deleted** | `src/rapl.c` |
| **Deleted** | `src/rapl.h` |

---

## Feature 2: `PINSIGHT_POWER` — Periodic Energy Snapshots via the Control Thread

**Goal:** Power-over-time profile — see how power varies across application phases.
**Mechanism:** The existing control thread polls energy at configurable per-platform
intervals and emits `energy_sample` tracepoints. Domain callbacks (OMPT, HIP, MPI,
Python) remain completely energy-free. Analysis correlates energy samples with domain
events by timestamp.

`PINSIGHT_POWER` reuses `energy.h` / `energy.c` from `PINSIGHT_ENERGY` unchanged.
No additional library dependencies.

### 2.1 CMake option

```cmake
option(PINSIGHT_POWER  "Power-over-time profiling via control thread polling"  FALSE)
```

### 2.2 Hardcoded interval defaults

Defined in `src/energy.h` — applied when the `[Power]` key is absent from the config:

```c
/* Default poll intervals — set at 10× hardware update rate to avoid redundant reads */
#define PINSIGHT_POWER_INTERVAL_INTEL_CPU_MS    10   /* RAPL updates ~1 ms */
#define PINSIGHT_POWER_INTERVAL_AMD_CPU_MS      10
#define PINSIGHT_POWER_INTERVAL_ARM_CPU_MS      50   /* platform-dependent */
#define PINSIGHT_POWER_INTERVAL_NVIDIA_GPU_MS  100   /* energy counter ~10 ms */
#define PINSIGHT_POWER_INTERVAL_AMD_GPU_MS     200   /* AMD-SMI higher read cost */
#define PINSIGHT_POWER_INTERVAL_INTEL_GPU_MS   100
```

### 2.3 Control thread — single timer, per-platform sub-rates

The control thread runs **one** `sem_timedwait` loop at `tick_ms` = GCD of all active
poll intervals. Each platform reads at its own sub-rate using tick modulus:

```c
static void *pinsight_control_loop(void *arg) {
    /* ... existing SIGUSR1 mask setup ... */
    uint64_t tick = 0;

    while (!control_shutdown) {
#ifdef PINSIGHT_POWER
        int power_active = (energy_power_config.tick_ms > 0);
#else
        int power_active = 0;
#endif
        if (power_active) {
            struct timespec deadline;
            clock_gettime(CLOCK_REALTIME, &deadline);
            long ns = (long)energy_power_config.tick_ms * 1000000L
                    + deadline.tv_nsec;
            deadline.tv_sec  += ns / 1000000000L;
            deadline.tv_nsec  = ns % 1000000000L;

            int ret = sem_timedwait(&control_sem, &deadline);
            if (ret == -1 && errno == ETIMEDOUT) {
                pinsight_control_energy_sample(++tick);
                continue;
            }
            /* sem_post: fall through to handle control event */
        } else {
            while (sem_wait(&control_sem) == -1 && errno == EINTR) {}
        }
        /* ... existing: SIGUSR1 reload, mode change, INTROSPECT ... */
    }
}
```

Per-platform read helper:

```c
static void pinsight_control_energy_sample(uint64_t tick) {
    energy_power_config_t *cfg = &energy_power_config;
    int tick_ms = cfg->tick_ms;
    pinsight_energy_t e = {0};

    /* follow_mode: skip the whole sample unless some domain is actively
       MONITORING/TRACING — phase-gated power, reuses region-of-interest config.
       This is a one-way READ of the domain mode flags; energy stays non-domain. */
    if (cfg->follow_mode && !pinsight_any_domain_active())
        return;

#define SHOULD_POLL(platform_ms) \
    ((platform_ms) > 0 && (tick % ((platform_ms) / tick_ms) == 0))

    /* poll_cpu/gpu variants use power_socket_mask / power_device_mask,
       which may differ from the socket_mask / device_mask used at enter/exit */
    if (SHOULD_POLL(cfg->intel_cpu.poll_interval_ms))   pinsight_energy_poll_intel_cpu(&e);
    if (SHOULD_POLL(cfg->amd_cpu.poll_interval_ms))     pinsight_energy_poll_amd_cpu(&e);
    if (SHOULD_POLL(cfg->arm_cpu.poll_interval_ms))     pinsight_energy_poll_arm_cpu(&e);
    if (SHOULD_POLL(cfg->nvidia_gpu.poll_interval_ms))  pinsight_energy_poll_nvidia_gpu(&e);
    if (SHOULD_POLL(cfg->amd_gpu.poll_interval_ms))     pinsight_energy_poll_amd_gpu(&e);
    if (SHOULD_POLL(cfg->intel_gpu.poll_interval_ms))   pinsight_energy_poll_intel_gpu(&e);

    lttng_ust_tracepoint(energy_pinsight_lttng_ust, energy_sample,
        (unsigned int)e.num_cpu_sockets, e.cpu_energy_uj,
        (unsigned int)e.num_gpu_devices, e.gpu_energy_mj,
        tick);   /* same sequence-based signature as energy_enter/exit */
}
```

For `PINSIGHT_POWER`, CPU sysfs reads should use **persistent fds** (opened at
`pinsight_energy_init()`, `read()` per poll tick) rather than `fopen`/`fclose` — the
read cost matters at ~10 ms intervals, unlike the twice-per-run `PINSIGHT_ENERGY` reads.

### 2.4 SIGUSR1 reload interaction

On config reload: re-parse `[Energy]` and `[Power]` sections, update
`energy_power_config`, recompute `tick_ms = GCD(all active intervals)` **and force
`tick_ms = 0` if `power_enabled == 0`**. The next loop iteration picks up the new `tick_ms`
naturally — enabling or disabling platforms, flipping the `enabled` master switch, changing
intervals, toggling `follow_mode`, or changing socket/device masks all take effect without
restart.

### 2.5 Runtime on/off control — three layered mechanisms

Three independent ways to turn polling on and off, in order of granularity. **None requires
new control-thread machinery** — they all funnel through the existing `tick_ms` / mode
state the control thread already reads each iteration.

**(1) Whole-feature master switch — `[Power] enabled = on/off` (static or live).**
`enabled = off` forces `tick_ms = 0` in `energy_power_config_finalize()`, so the control
loop stays on the blocking `sem_wait` path — zero overhead, identical to `PINSIGHT_POWER`
unset. To toggle **live**, edit the config and `kill -USR1 <pid>`: the existing reload
([pinsight_control_thread.c:210](src/pinsight_control_thread.c#L210)) re-parses and the
accompanying `sem_post` wakes the loop, so the change lands on the very next iteration. No
new mechanism — this reuses SIGUSR1 reload (§2.4).

**(2) Phase-gated polling — `[Power] follow_mode = on`.** When set, the sample helper polls
only while at least one domain is in `MONITORING`/`TRACING` (the gate at the top of
`pinsight_control_energy_sample`). This makes the power profile automatically track the
regions of interest the user already configured via lexgion mode control, with **no app
changes**. It is a one-way *read* of `domain_default_trace_config[d].mode` — energy is not
made a domain, consistent with the "Energy/Power is NOT a domain" decision. Implement a
small helper:

```c
/* in pinsight_control_thread.c — true if any domain is MONITORING or TRACING */
int pinsight_any_domain_active(void) {
    for (int d = 0; d < num_domain; d++) {
        pinsight_domain_mode_t m = domain_default_trace_config[d].mode;
        if (m == PINSIGHT_DOMAIN_MONITORING || m == PINSIGHT_DOMAIN_TRACING)
            return 1;
    }
    return 0;
}
```

**(3) Programmatic API — `pinsight_power_on()` / `pinsight_power_off()`.** For precise,
source-level region-of-interest control the application brackets a region directly. These
set a `volatile int power_runtime_gate` checked alongside `follow_mode` in the sample
helper (gate closed → skip sampling), and are exported from `enter_exit.c` like the other
app-facing entry points. The most precise of the three, at the cost of touching app source.

```c
/* public API (declared in a pinsight app header, defined in enter_exit.c) */
void pinsight_power_off(void);   /* close the gate — stop emitting energy_sample */
void pinsight_power_on(void);    /* open the gate — resume */
```

The three compose: `enabled` is the master (off ⇒ nothing polls); within that,
`follow_mode` and the API gate *whether a given tick emits a sample*. The poll-interval
keys still set the *rate*.

### 2.6 Analysis — what `PINSIGHT_POWER` gives you

`energy_sample` events are interleaved with OMPT/HIP/MPI/Python events on the same LTTng
timeline. Per-phase power attribution by linear interpolation:

```python
samples = trace.find_events("energy_pinsight_lttng_ust:energy_sample")
regions = trace.find_pairs("ompt_pinsight_lttng_ust:parallel_begin",
                           "ompt_pinsight_lttng_ust:parallel_end")

for region in regions:
    before = last_sample_before(samples, region.t_begin)
    after  = first_sample_after(samples, region.t_end)
    fraction = (region.t_end - region.t_begin) / (after.ts - before.ts)
    energy_J = (after["cpu_uj"][0] - before["cpu_uj"][0]) / 1e6 * fraction  # socket 0
    power_W  = energy_J / ((region.t_end - region.t_begin) / 1e9)
```

### 2.7 `PINSIGHT_POWER` file change summary

| Action | File |
|--------|------|
| **Modified** | `src/energy.h` — add `pinsight_energy_poll_cpu/gpu()` using `power_socket/device_mask`; add interval defaults |
| **Modified** | `src/energy.c` — implement poll variants + persistent fds for CPU sysfs polling path |
| **Modified** | `src/energy_lttng_ust_tracepoint.h` — add `energy_sample` tracepoint |
| **Modified** | `src/pinsight_control_thread.c` — timed loop + `energy_sample` emission; `pinsight_any_domain_active()`; `follow_mode`/runtime-gate checks |
| **Modified** | `src/enter_exit.c` — export `pinsight_power_on()` / `pinsight_power_off()` + `power_runtime_gate` |
| **Modified** | `src/trace_config.h` — add poll intervals + `power_enabled` + `follow_mode` to `energy_power_config_t` |
| **Modified** | `src/trace_config_parse.c` — parse `[Power]` section (incl. `enabled`, `follow_mode`); force `tick_ms=0` when disabled; compute `tick_ms` |

---

## Platform Support Summary

| Platform | sysfs / Library | Hardware update rate | Default poll interval | CMake flag |
|----------|-----------------|---------------------|-----------------------|------------|
| Intel CPU | `/sys/class/powercap/intel-rapl/*/energy_uj` | ~1 ms | 10 ms | `PINSIGHT_ENERGY` |
| AMD CPU (EPYC) | `/sys/class/hwmon/hwmon*/energy*_input` (amd_energy) | ~1 ms | 10 ms | `PINSIGHT_ENERGY` |
| ARM CPU | `/sys/class/hwmon/hwmon*/energy*_input` (best effort) | ~10–50 ms | 50 ms | `PINSIGHT_ENERGY` |
| NVIDIA GPU | `nvmlDeviceGetTotalEnergyConsumption` | ~10 ms | 100 ms | `PINSIGHT_ENERGY_NVIDIA` |
| AMD GPU / MI300A | `amdsmi_get_energy_count` | ~10 ms | 200 ms | `PINSIGHT_ENERGY_AMD_GPU` |
| Intel GPU | `zesPowerGetEnergyCounter` | ~10 ms | 100 ms | `PINSIGHT_ENERGY_INTEL_GPU` |

**MI300A note:** AMD-SMI reports combined CPU+GPU+HBM package energy — hardware does not
expose per-component breakdown. Tracepoint field labeled `apu_package_energy_mj`.

---

## Implementation Order

> **Dev order vs. payoff order.** The steps below start with Intel CPU sysfs because it is
> the fastest to validate (any Intel Linux box, no library deps) and proves the whole
> pipeline — `energy.c` → dedicated tracepoint → `lttng view`. But the **deliverable for
> this project's target hardware (Tuolumne/El Capitan MI300A) is the AMD GPU / APU path**
> (`PINSIGHT_ENERGY_AMD_GPU` via AMD-SMI, step 6), which yields the combined APU package
> energy. Don't treat steps 1–5 as the goal; they are scaffolding to de-risk step 6. On
> MI300A the CPU-sysfs snapshots (steps 1–2) may be absent or redundant.

### `PINSIGHT_ENERGY` steps

1. Write `src/energy.h` + `src/energy.c` — Intel CPU sysfs path only, no GPU.
   Test on Intel Linux with `-DPINSIGHT_ENERGY=TRUE`. Verify `energy_enter` /
   `energy_exit` appear in `lttng view` with correct µJ values.
2. Add AMD CPU hwmon path. Test on an EPYC system.
3. Remove old RAPL code: delete `src/rapl.c` / `src/rapl.h`, remove `ENERGY_LTTNG_*`
   macros from `ompt_lttng_ust_tracepoint.h`, remove energy blocks from `ompt_callback.c`.
4. Add `[Energy]` config parsing in `trace_config_parse.c` — enable/disable per platform,
   socket/device index list → bitmask conversion.
5. Add NVIDIA GPU path + CMake find for NVML. Test on a CUDA system.
6. Add AMD GPU path + CMake find for AMD-SMI. Test on El Capitan after ROCTracer
   validation.
7. Add Intel GPU path + CMake find for Level-Zero (lower priority).

### `PINSIGHT_POWER` steps

8. Add per-platform interval defaults to `src/energy.h`. Add persistent fd path in
   `src/energy.c` for CPU sysfs reads used during polling.
9. Add `[Power]` section parsing in `trace_config_parse.c`. Implement `tick_ms` GCD
   computation.
10. Add `energy_sample` tracepoint. Extend control thread with timed loop. Test with
    `intel_cpu_poll_interval_ms = 1000`, verify samples appear at ~1 s intervals.
11. Implement analysis-side linear interpolation in the Python analysis tool.

---

## Supplementary Details

Details that are too low-level for the main plan but needed for correct implementation.

### GCD helper and tick_ms edge cases

```c
static int gcd(int a, int b) { return b == 0 ? a : gcd(b, a % b); }

static int compute_tick_ms(energy_power_config_t *cfg) {
    int intervals[] = {
        cfg->intel_cpu.poll_interval_ms,
        cfg->amd_cpu.poll_interval_ms,
        cfg->arm_cpu.poll_interval_ms,
        cfg->nvidia_gpu.poll_interval_ms,
        cfg->amd_gpu.poll_interval_ms,
        cfg->intel_gpu.poll_interval_ms,
    };
    int result = 0;
    for (int i = 0; i < 6; i++) {
        if (intervals[i] > 0)
            result = (result == 0) ? intervals[i] : gcd(result, intervals[i]);
    }
    return result;  /* 0 means no polling active — control thread uses sem_wait */
}
```

When `tick_ms == 0`, the control thread stays on the original blocking `sem_wait` path —
no periodic wakeups, zero overhead, identical to `PINSIGHT_POWER=FALSE`.

### Persistent fd pattern for CPU sysfs polling

For `PINSIGHT_POWER`, each sysfs energy file is opened once at `pinsight_energy_init()`
and held open. A `read()` on a sysfs file fd resets the file offset and returns the
current counter value — the same as `fopen`/`fscanf`/`fclose` but without the open/close
overhead:

```c
static int cpu_poll_fds[MAX_ENERGY_PACKAGES];  /* opened at init, -1 if inactive */

/* At init (for each enabled socket): */
cpu_poll_fds[s] = open(cpu_sysfs_paths[s], O_RDONLY);

/* At each poll tick: */
void pinsight_energy_poll_intel_cpu(pinsight_energy_t *e) {
    char buf[32];
    for (int s = 0; s < num_discovered_sockets; s++) {
        if (!(cfg->intel_cpu.power_socket_mask & (1ULL << s))) continue;
        if (cpu_poll_fds[s] < 0) continue;
        lseek(cpu_poll_fds[s], 0, SEEK_SET);       /* rewind sysfs file */
        int n = read(cpu_poll_fds[s], buf, sizeof(buf) - 1);
        if (n > 0) { buf[n] = '\0'; e->cpu_energy_uj[s] = strtoull(buf, NULL, 10); }
    }
}

/* At fini: */
for (int s = 0; s < MAX_ENERGY_PACKAGES; s++)
    if (cpu_poll_fds[s] >= 0) { close(cpu_poll_fds[s]); cpu_poll_fds[s] = -1; }
```

The `lseek(fd, 0, SEEK_SET)` + `read()` pattern is the standard way to re-read a sysfs
counter without reopening the file. Cost: ~1–3 µs per socket, acceptable at 10 ms intervals.

### Config parser two-pass logic (pseudocode)

```c
void parse_energy_section(const char *key, const char *value) {
    if      (strcmp(key, "intel_cpu")          == 0) cfg.intel_cpu.enabled       = parse_onoff(value);
    else if (strcmp(key, "intel_cpu_sockets")  == 0) cfg.intel_cpu.socket_mask   = parse_index_list(value);
    else if (strcmp(key, "amd_cpu")            == 0) cfg.amd_cpu.enabled         = parse_onoff(value);
    else if (strcmp(key, "amd_cpu_sockets")    == 0) cfg.amd_cpu.socket_mask     = parse_index_list(value);
    /* ... nvidia_gpu, amd_gpu, intel_gpu ... */
}

void parse_power_section(const char *key, const char *value) {
    if      (strcmp(key, "intel_cpu_poll_interval_ms") == 0) cfg.intel_cpu.poll_interval_ms  = atoi(value);
    else if (strcmp(key, "intel_cpu_sockets")          == 0) cfg.intel_cpu.power_socket_mask = parse_index_list(value);
    /* ... etc ... */
}

/* After both sections parsed: */
void energy_power_config_finalize(void) {
    /* Inherit socket/device masks from [Energy] where [Power] didn't override */
    if (cfg.intel_cpu.power_socket_mask == 0 && cfg.intel_cpu.poll_interval_ms > 0)
        cfg.intel_cpu.power_socket_mask = cfg.intel_cpu.socket_mask;
    /* ... same for all platforms ... */
    cfg.tick_ms = compute_tick_ms(&cfg);
    /* whole-feature master switch: off ⇒ no polling regardless of intervals */
    if (!cfg.power_enabled)
        cfg.tick_ms = 0;
}
```

Call `energy_power_config_finalize()` after parsing the entire config file, not after
each section — both sections must be fully parsed before resolving defaults.

### `parse_index_list` — converting "0,2-4" to a mask

**Correction (verified 2026-06-10): reuse the existing parser; do NOT hand-roll `uint64_t`.**

`trace_config_parse.c` already parses exactly this "0,2-4,6" / range syntax via
`parse_range_list()` → `bitset_parse_ranges(BitSet *mask, const char *str)`
([trace_config_parse.c:188](src/trace_config_parse.c#L188)). The codebase's mask type is
`BitSet`, not `uint64_t`. Reuse it directly:
- Consistency with how punits are already parsed and stored
- Removes the silent 64-index cap baked into the `uint64_t` sketch below
- `"all"` handling and comma/range tokenizing already implemented and tested

Therefore the runtime config struct (see "Runtime Config Structure") should use `BitSet`
for `socket_mask` / `device_mask` / `power_socket_mask` / `power_device_mask` rather than
`uint64_t`, and the read/poll loops should test membership with the `BitSet` accessor
(e.g. `bitset_test(&mask, s)`) instead of `mask & (1ULL << s)`. The `uint64_t` snippets
throughout this plan are illustrative of the *logic* only — implement them against `BitSet`.

The original `uint64_t` sketch (kept for reference — **do not implement as-is**):

```c
/* SUPERSEDED by bitset_parse_ranges() — illustrative only */
uint64_t parse_index_list(const char *s) {
    if (strcmp(s, "all") == 0) return ~0ULL;
    uint64_t mask = 0;
    char buf[64]; strncpy(buf, s, sizeof(buf));
    char *tok = strtok(buf, ",");
    while (tok) {
        char *dash = strchr(tok, '-');
        if (dash) { int lo=atoi(tok), hi=atoi(dash+1); for(int i=lo;i<=hi&&i<64;i++) mask|=(1ULL<<i); }
        else      { int idx=atoi(tok); if(idx>=0&&idx<64) mask|=(1ULL<<idx); }
        tok = strtok(NULL, ",");
    }
    return mask;
}
```

### Default socket/device mask at `pinsight_energy_init()`

When no `[Energy]` config has been parsed (or a platform key is absent), the mask should
default to "all discovered sockets/devices":

```c
/* After sysfs discovery finds num_discovered_sockets = N: */
if (cfg->intel_cpu.socket_mask == 0)           /* not set by config */
    cfg->intel_cpu.socket_mask = (1ULL << N) - 1;  /* all N sockets */
```

Apply the same defaulting for GPU device masks after NVML/AMD-SMI enumeration.
