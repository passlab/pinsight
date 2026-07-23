# Node-Singleton Measurement Design (`device_activity` + energy `measure`)

**Date:** 2026-07-23
**Author:** Yonghong Yan
**Status:** Phase 1 IMPLEMENTED & hardware-validated (2026-07-23, build_eval on Tuolumne);
Phases 2–3 designed, not yet implemented. See §6.0.
**Related:** [energy_power_implementation_plan.md](energy_power_implementation_plan.md),
code-memory `amg2023_eval_overhead`, `hip_activity_overhead_refs`, `energy_power_support`

---

## 1. Summary (TL;DR)

Two PInsight measurements benefit from running on **exactly one process per node**
instead of every rank:

1. **GPU device-activity tracing (HIP/CUDA)** — running the ROCTracer/CUPTI activity
   pool on all ranks of a node causes a large, jitter-driven overhead at scale
   (measured **+16.9%** solve time on 16×4 AMG2023). Collecting on one rank/node
   drops it to **+3.8%** (near the host-tracing floor) while keeping GPU-exec data for
   one GPU/node.
2. **Energy/power measurement** — every rank reads the *same* node-local energy
   counters, so a 4-rank node counts node energy 4× (a documented caveat in the energy
   plan). One rank/node produces one clean node-energy series.

We add a **node-singleton** capability to exactly these two measurement families
(**not** to general domains like OpenMP/MPI/Python). It is expressed by a single
config value with four states:

```
off | on | anyone_per_node | leader_per_node
```

- **HIP/CUDA:** `[HIP.default] device_activity = …`, `[CUDA.default] device_activity = …`
- **Energy:** `[Energy] measure = …`

`anyone_per_node` = one arbitrary rank/node (lock-file election). `leader_per_node` =
one deterministic node-leader rank (env → local-rank → MPI, lock-file as last resort).

---

## 2. Motivation

### 2.1 The HIP activity-pool overhead (the driving finding)

On Tuolumne (4× MI300A/node), full HIP tracing of AMG2023 at 16 nodes × 4 ranks/node
adds **+16.9%** to the communication-bound solve phase (setup ≈ 0; total ≈ +2%).
Root-cause investigation (see code-memory `amg2023_eval_overhead`) established:

- The cost is the **GPU activity-record pool (ROCTracer `ACTIVITY_DOMAIN_HIP_OPS`
  collection)**, not host callbacks and not MPI tracing. Disabling only the activity
  pool (`hostonly`) drops 16×4 to +1.3%.
- It is a **3-way interaction**: activity collection **×** ≥3 activity-collectors/node
  **×** cross-node collective coupling. Remove any leg → ≈ +1%.
- **Density threshold is a sharp step between 2 and 3 ranks/node** (not a gradient,
  not a 4-APU cliff):

  | ranks/node | 1 | 2 | 3 | 4 |
  |---|---|---|---|---|
  | full-tracing solve overhead | +1.0% | +0.5% | **+12.2%** | +13.2% |

- **Mechanism = activity-collection jitter amplified by the global `MPI_Allreduce`.**
  The CG dot-products (`hypre_ParVectorInnerProd`) fuse a GPU reduction +
  `hipStreamSynchronize` directly into a 1-element 64-way `MPI_Allreduce`. With ≥3
  ranks/node contending on the shared per-node profiling path, each rank reaches the
  barrier at a *variable* time; the barrier waits for the max-of-64 arrival. Measured
  signature: `MPI_Allreduce` per-call median inflates **~8×** (740 µs → 5751 µs) while
  cross-rank CV *drops* (0.52 → 0.31) — the fingerprint of random jitter redistributed
  by a global barrier. `MPI_Waitall` (point-to-point halo, ~6 neighbors) is unaffected.

- **Validated fix — rank-gating (1 collector/node):** solve overhead **+16.9% → +3.8%**
  (host-tracing floor is +2.9%; residual +0.9% from the 16 persistent-collector ranks).
  Mechanistically confirmed: with 1 collector/node the `MPI_Allreduce` median stays at
  baseline (679 µs vs 740 µs) — the jitter amplification is gone. Trade-off: activity
  data covers **1 GPU/node** instead of 4.

- **External corroboration** (see `hip_activity_overhead_refs`): the amplification is a
  known OS-noise phenomenon (Sandia); ROCm's own `rocprofv3` ships the same two
  mitigations we derived — **subset-rank profiling** (community practice via launcher
  wrappers) and **time-windowed collection** (`--collection-period`).

### 2.2 The energy multi-count problem

Per [energy_power_implementation_plan.md](energy_power_implementation_plan.md) lines
122–124: *"every rank's libpinsight reads the same 4 node-local APU counters. For node
energy, use one rank per hostname; summing across ranks on the same node
multiple-counts."* Today this dedup is punted to the analysis side. A node-singleton
gate moves it into the tool.

### 2.3 Common need

Both are **node-level (or node-shared-resource) measurements** where more than one
process per node is wasteful (energy) or actively harmful (activity jitter). They share
one primitive: *run this measurement on exactly one process per node.*

---

## 3. Scope

**In scope** — node-singleton support for:
- HIP device activity: `[HIP.default] device_activity`
- CUDA device activity: `[CUDA.default] device_activity`
- Energy/power: `[Energy] measure`

**Out of scope** — the node-singleton concept is deliberately **not** offered for the
event-driven domains (OpenMP, MPI, Python). Those are inherently per-rank/per-thread;
"trace one rank per node" for them is a general sampling feature we are not building
here (it caused confusion in design discussion and has no data behind it yet).

---

## 4. Design decisions and options considered

### 4.1 Enforcement: per-process capability gate (NOT punit, NOT mode-machine)

| option | verdict |
|---|---|
| **Punit-based** (a node-local-rank punit) | **Feasible, but rejected for clarity/complexity.** It *can* express and implement the gate: a node-local-rank punit's range could be consulted at **capability-setup** (pool-open / energy-init), not just at the per-event trace-bit ([trace_config.c:77-84](../src/trace_config.c#L77-L84)) — so the config semantics are coherent. The problem is it **overloads punit meaning**: punits normally gate *event emission*, so using one to gate a per-process *capability* is a second, special semantic requiring special-case enforcement. (A *naive* per-event punit would additionally fail — it would leave the pool collecting on all ranks with no overhead benefit, and would wrongly filter host events too.) More confusing to users and more code than a dedicated value. |
| **Mode-machine** (start the whole domain OFF on non-selected ranks) | **Rejected for this scope.** Works and is general, but too coarse (gates the entire HIP domain, killing host tracing) and conflated two different use cases. |
| **Per-process capability gate at measurement setup** (open the activity pool / init the energy backend only on the selected process) | **Chosen.** Gates exactly the expensive/duplicated capability; host tracing stays on all ranks; energy source masks unaffected. Clear one-value config, no punit overloading. |

### 4.2 Which process runs the singleton — policy values

Mutually exclusive states collapsed into a **single config key** (an earlier
two-key form `device_activity` + `device_activity_once_per_node` was rejected for the
awkward "subkey only meaningful when key=on" dependency). `device_activity` has five
values (incl. `rotate_per_node`); energy `measure` has the first four:

| value | meaning | election mechanism |
|---|---|---|
| `off` | disabled | — |
| `on` | all ranks measure/collect | — |
| `anyone_per_node` | exactly one **arbitrary** rank/node | atomic lock file |
| `leader_per_node` | exactly one **deterministic leader** rank/node | env → local-rank → MPI → lock fallback |
| `rotate_per_node:<ms>` | one collector/node, **rotating** every `<ms>` so all N GPUs are sampled over time (device_activity only) | time-derived from node clock (§6.10) |

`rotate_per_node` takes an optional period suffix (`rotate_per_node:1000`); bare
`rotate_per_node` uses a default period. It is valid **only** for `device_activity`
(HIP/CUDA) — energy is node-level, so `measure` accepts only the first four values
(`rotate_per_node` in `[Energy]` → warn + ignore).

**Value-naming rationale:** `once_per_node` as a separate key was dropped; the concept
lives in the value suffix. Bare `any`/`leader` were rejected as semantically thin
(`any` does not convey "any *one*"). `anyone_per_node` explicitly means "any single
one" and pairs symmetrically with `leader_per_node`. A global `node_leader` mechanism
knob was considered and **dropped** — resolution is automatic and internal; users pick
only the policy.

### 4.3 Leader resolution and the constructor-timing constraint

The hard constraint: some measurements are set up at the **library constructor, before
`MPI_Init`**, so MPI-based election is not yet available for them.

| measurement | decision needed at | MPI available? |
|---|---|---|
| energy enter-snapshot + backend init | constructor | ❌ pre-`MPI_Init` |
| energy exit-snapshot | shutdown | ✅ |
| power polling (control thread) | ongoing | ✅ post-`MPI_Init` |
| HIP/CUDA activity pool — eager (TRACING start) | constructor | ❌ |
| HIP/CUDA activity pool — lazy (STANDBY→ACTIVE) | control-thread transition | ✅ |

`leader_per_node` therefore resolves via a **layered chain** (consistent with
PInsight's existing env-guess-then-MPI-confirm pattern for `mpirank`):

1. **Launcher-designated env** `PINSIGHT_NODE_LEADER=1` (set by the job script) — the
   primary, cleanest path; constructor-safe, launcher-agnostic (the script owns the
   launcher-specific local-rank var).
2. **PInsight reads a local-rank env chain** — `FLUX_TASK_LOCAL_ID` → `SLURM_LOCALID`
   → `OMPI_COMM_WORLD_LOCAL_RANK` → `MPI_LOCALRANKID`; leader = local_rank 0. No script
   change. (`FLUX_TASK_LOCAL_ID` verified present on Tuolumne = 0,1,2,3.)
3. **MPI `MPI_Comm_split_type(SHARED)`** in the `MPI_Init` wrapper — authoritative,
   launcher-agnostic; used directly for the *deferrable* measurements and as a
   post-`MPI_Init` confirmation for the eager ones.
4. **Lock-file `anyone_per_node` fallback** — if no env and no MPI, degrade to arbitrary
   one-per-node (with a stderr note) so we still guarantee exactly one/node.

**Why the eager env-guess is low-stakes:** for both target measurements a wrong guess is
harmless — energy is node-level (any single rank reads the same counters), and activity
just samples a different one of the 4 APUs. So determinism (reproducible local-rank-0)
holds in the normal case, and even the fallback yields correct one-per-node data. Only
the leader ever initializes AMD-SMI, side-stepping the AMD-SMI teardown gotcha for the
other ranks.

**IPC (shared memory) was considered and rejected:** deterministic lowest-rank election
at the constructor would need a barrier (knowing all node processes have arrived) — racy
without MPI and buys nothing the env-var path doesn't already give deterministically.

### 4.4 `anyone_per_node` election — the lock file

`flock(fd, LOCK_EX|LOCK_NB)` on a node-local `/tmp/$USER/pinsight_<name>_singleton.lock`
(`/tmp` is node-local and per-job on Tuolumne). Exactly one process wins and holds the
fd for its lifetime; losers skip. `flock` auto-releases on process death → no stale
files, no cleanup. Works at constructor time, no MPI, launcher-agnostic.

**Independent singletons:** each measurement uses its **own lock name** (`"energy"`,
`"hip_activity"`, `"cuda_activity"`) so their winners are independent — the energy
singleton and the activity singleton may land on different ranks; that is fine and
intended.

---

### 4.5 Temporal scope — nodepolicy is per-RUN, windowing is the mode machine's job

The env var is one-time (startup); config is reloadable (SIGUSR1) and normally changes
runtime behavior per **window** (a window = a span of execution with one tracing config;
a domain's `trace_mode` can flip OFF/STANDBY/MONITORING/TRACING between windows). Does the
nodepolicy (WHICH ranks measure) change per window?

**Decision: no — the nodepolicy is resolved ONCE at startup and is fixed for the run.** It
decomposes into two parts that must both be stable:
- **Election** (which specific rank is the singleton): `leader_per_node` is deterministic
  (always local-rank-0); `anyone_per_node` holds its flock for the process lifetime — so the
  singleton identity is inherently per-run. Re-electing per window would make the energy
  series / the collecting GPU **hop between ranks** (broken attribution) and add flock
  release/re-acquire races.
- **Policy value** (off/on/anyone/leader): also fixed at startup. Env sets it once; config
  sets the startup value; **reload does not change it** (the parser warns if a reload
  presents a different value).

**Per-window control of *when* measurement happens is delegated to the existing mode
machine, not the nodepolicy.** For HIP/CUDA the effective gate composes as:
```
collect_now = nodepolicy_eligible(this_rank)   /* fixed per-run : WHO  */
              AND trace_mode == TRACING          /* per-window    : WHEN */
```
A collector rank's activity pool already follows its `trace_mode` per window
(TRACING→collect, MONITORING/STANDBY→pause, OFF→teardown); a non-collector never opens the
pool regardless of mode. So per-window activity on/off is already available via `trace_mode`.
For energy (enter/exit are per-run; power polling is continuous) phase-gating is the energy
plan's `follow_mode` — again orthogonal to the nodepolicy.

**Consequence (documented constraint):** `device_activity` and `[Energy] measure` are
**startup-fixed**; to change *which ranks* measure, restart. To change *whether* a collector
is actively tracing within a run, use `trace_mode` (HIP/CUDA) or `follow_mode` (energy
power). Env and config are therefore identical in effect for the nodepolicy; env just
overrides the startup value (§6.2).

**Planned extensions (phased — see §6.0):** two additive capabilities build on this base
without violating "election is per-run":
- **Phase 2 — per-window activity toggle:** re-read the `device_activity` *policy value*
  (not the election) at each mode transition / reload, so a collector can switch
  host-on/activity-*off* per window (e.g. activity in setup, off in the comm-bound solve).
  The `on↔off` toggle is live; the elected collector set stays fixed. Changing the
  *selection method* (leader↔anyone) on reload is warned + ignored.
- **Phase 3 — round-robin (`rotate_per_node`):** the collector *rotates* over time so all
  N GPUs are sampled at 1-collector overhead — the only meaningful "vary who over time"
  (re-electing anyone→anyone / leader→leader is a semantic no-op). Time-derived, no IPC
  (§6.10).

---

## 5. The config contract (final decision)

```ini
[Energy]
measure = off | on | anyone_per_node | leader_per_node     # default: on (preserves current behavior)
                                                            # NOT trace_mode — energy is not a domain
amd_gpu         = on            # per-platform source selection still applies underneath
amd_gpu_devices = 0-3           # the measuring rank reads all 4 APUs

[HIP.global]
# [global] declares AVAILABLE options. The value-SETS below are hardcoded in the enum/code,
# so these lines are IGNORED today (informational only), exactly like trace_mode's:
#   trace_mode      = OFF, MONITOR, STANDBY, TRACING
#   device_activity = off, on, anyone_per_node, leader_per_node, rotate_per_node
HIP.device = (0, 16)          # punit RANGE (this one is used)
[HIP.default]
trace_mode      = TRACING     # actual SETTINGS live in [default]
device_activity = off | on | anyone_per_node | leader_per_node | rotate_per_node:<ms>   # default: off

[CUDA.default]
device_activity = off | on | anyone_per_node | leader_per_node | rotate_per_node:<ms>   # default: off
```

> **Config-section convention:** `[Domain.global]` declares **available options** — the
> value-*sets* of `trace_mode` and nodepolicy keys, and the *ranges* of punits (e.g.
> `OpenMP.thread = 0:16`). The value-sets for `trace_mode`/nodepolicy are hardcoded in the
> enum, so those `[global]` lines are **ignored today** (informational). The **actual
> settings** — `trace_mode`, `device_activity`, per-event on/off — go in `[Domain.default]`
> and are stored in `domain_default_trace_config[]`. So for this feature the parser reads
> `device_activity` from `[Domain.default]`; no `[global]` parsing is added.

**Defaults & behavior changes:**
- `device_activity` default **off** — activity is opt-in (it is the expensive path; this
  is a behavior change from "activity on whenever HIP active", intended per §2.1). This
  **removes** the `PINSIGHT_HIP_DISABLE_ACTIVITY` env var (config-only).
- `measure` default **on** — preserves today's all-ranks energy behavior; `leader_per_node`
  is the recommended setting. Also settable via env **`PINSIGHT_MEASURE_ENERGY`**
  (`ON|OFF|ANYONE_PER_NODE|LEADER_PER_NODE`), which **overrides** the config key. (The env/config
  asymmetry is **intentional**: energy `measure` is the *general, end-user-facing* knob, so
  a simple env var is the convenient control; `device_activity` is an *advanced* knob that
  rides the domain-config/DSL framework and is config-only — `PINSIGHT_HIP_DISABLE_ACTIVITY`
  is removed. Energy also has no config plumbing yet, so the env is its first runtime
  control.)

**Semantics:**
- For **Energy**, `measure` gates whole-process participation (both enter/exit snapshots
  and power polling). Per-platform enables (`amd_gpu`, `intel_cpu`) and socket/device
  masks still choose *what* the measuring rank reads — orthogonal, `measure` is the outer
  "who" gate with higher priority.
- For **HIP/CUDA**, `device_activity` gates only the activity pool; host HIP/CUDA tracing
  stays on all ranks regardless.

**Recommended production config:**
```ini
[Energy]
measure = leader_per_node
[HIP.default]
trace_mode      = TRACING
device_activity = leader_per_node
```

---

## 6. Detailed implementation plan

Built in layers so each step compiles and is testable on its own. Suggested commit
boundaries match the sub-sections.

> **Phase 1 status (2026-07-23): DONE & validated.** Implemented across
> trace_config.{h,c} (enum/structs/resolver + energy_measure_policy global),
> trace_domain_dsl.h + trace_domain_loader.{h,c} (`TRACE_NODEPOLICY`/`dsl_add_nodepolicy`),
> trace_domain_{HIP,CUDA}.h (declare `device_activity`), trace_config_parse.c
> (`[Domain.default] device_activity` + `[Energy] measure`), roctracer_callback.c +
> cupti_callback.c (pool gate; `PINSIGHT_HIP_DISABLE_ACTIVITY` removed), energy.{h,c} +
> enter_exit.c (energy gate + config-before-init reorder). No new source files. Builds
> clean (libpinsight.so + test_config_parser). **Hardware test (1 node, AMG):**
> `device_activity=off`→0 activity / 2821 host launches; `on`→2821/2821; `leader_per_node`
> →2821/2821 (single rank = leader); `energy_enter=1` all cases. CUDA path mirrors HIP but
> is untested (no NVIDIA hardware). Eval `run_rep.sh` migrated to config-based
> `device_activity`.

### 6.0 Implementation order (three phases, each a superset of the last)

- **Phase 1 — base per-run nodepolicy (§6.1–6.8).** `off | on | anyone_per_node |
  leader_per_node` for `device_activity` (HIP/CUDA) + energy `measure`. Resolved once at
  startup; gate at pool-open / energy-init. **This is the validated fix** (16×4 +16.9%→
  +3.8%) and delivers the immediate value. Ship first.
- **Phase 2 — per-window activity toggle (§6.9).** Re-read the `device_activity` *policy
  value* live at mode transitions / reload (election stays per-run) so a collector can be
  host-on / activity-*off* per window — e.g. activity in setup, off in the comm-bound
  solve. ~½–1 day; reuses `apply_mode` + the proven cyclic enable/disable path.
- **Phase 3 — round-robin `rotate_per_node:<ms>` (§6.10).** Rotate the single collector
  across ranks over time → all N GPUs sampled at 1-collector overhead, and the
  persistent-straggler residual spread out. ~1–2 days; reuses Phase 2's toggle driven by a
  control-thread timer. HIP/CUDA only.

### 6.1 Config representation — DSL-declared nodepolicy keys (no new file)

The policy is a **first-class, DSL-declared, per-domain concept, parallel to events and
punits** — no standalone module. This matches the existing `dsl_add_punit` /
`TRACE_PUNIT` pattern exactly.

**Enum — in `trace_config.h`** (shared by the HIP/CUDA domains *and* energy):
```c
typedef enum {
    PINSIGHT_NODEPOLICY_OFF = 0,          /* skip everywhere             */
    PINSIGHT_NODEPOLICY_ON,               /* every rank measures         */
    PINSIGHT_NODEPOLICY_ANYONE_PER_NODE,  /* 1 arbitrary rank/node       */
    PINSIGHT_NODEPOLICY_LEADER_PER_NODE,  /* 1 deterministic leader/node */
    PINSIGHT_NODEPOLICY_ROTATE_PER_NODE,  /* 1 rotating collector/node (device_activity only) */
} pinsight_nodepolicy_t;

/* Runtime value = policy + optional param (rotate period ms; 0 unless ROTATE). */
typedef struct { pinsight_nodepolicy_t policy; int param; } pinsight_nodepolicy_val_t;
```

**DSL surface macro — `trace_domain_dsl.h`** (mirrors `TRACE_PUNIT`):
```c
#define TRACE_NODEPOLICY(name, default_policy)  TRACE_IMPL_NODEPOLICY(name, default_policy)
```
Each domain header defines `TRACE_IMPL_NODEPOLICY(name, dflt)` →
`dsl_add_nodepolicy((name), (dflt));` — a new builder in `trace_domain_loader.{h,c}`
alongside `dsl_add_punit`, that appends to the current domain's `nodepolicy_keys[]`.

**Declared in the domain headers** (`trace_domain_HIP.h`, `trace_domain_CUDA.h`), inside
the `TRACE_DOMAIN_BEGIN(...) … TRACE_DOMAIN_END()` block next to the `TRACE_PUNIT` line:
```c
TRACE_NODEPOLICY("device_activity", PINSIGHT_NODEPOLICY_OFF)
```

**Index constant for gate lookup (#2).** Each domain also defines a fixed index constant
(declaration order) so gate sites index the value array directly rather than string-match
by name — e.g. in `trace_domain_HIP.h`: `#define HIP_NODEPOLICY_DEVICE_ACTIVITY 0` (CUDA
analog). Validated once at init: assert `nodepolicy_keys[idx].name == "device_activity"`,
so a reorder is caught immediately.

**Struct changes — `trace_config.h`:**
- `struct domain_info` (static descriptor) — add next to `punits[]` / `num_punits`:
  ```c
  struct nodepolicy_key {
      char name[32];                 /* e.g. "device_activity"                    */
      pinsight_nodepolicy_t dflt;    /* declared default (OFF for device_activity) */
  } nodepolicy_keys[MAX_NUM_NODEPOLICY_KEYS];
  int num_nodepolicy_keys;
  ```
- `domain_trace_config_t` (runtime) — add the parsed-values array (same index order as
  `nodepolicy_keys[]`), each carrying policy + optional param:
  ```c
  volatile pinsight_nodepolicy_val_t nodepolicy[MAX_NUM_NODEPOLICY_KEYS];
  ```
- `#define MAX_NUM_NODEPOLICY_KEYS 4` (small).

**Default-copy at init** — mirror the existing events/mode copy loop
([trace_config.c:283-291](../src/trace_config.c#L283-L291)):
```c
for (k = 0; k < domain_info_table[i].num_nodepolicy_keys; k++)
    domain_default_trace_config[i].nodepolicy[k] =
        domain_info_table[i].nodepolicy_keys[k].dflt;
```

**Energy** (not a domain, untouched per your note) reuses the *same enum* but stores its
value in `energy_power_config.measure` — consistent with "Energy/Power is NOT a domain".

### 6.2 Parser + runtime resolver (both in `trace_config.{h,c}` — no new file)

**Parser (generic, no per-key special-casing)** — in `trace_config_parse.c`, the
`device_activity` *setting* is read from **`[DOMAIN.default]`** (where `trace_mode` and the
per-event on/off also live), not `[DOMAIN.global]`. When a `[DOMAIN.default]` key is not
`trace_mode` / an event, scan the domain's `nodepolicy_keys[]` by name; on match,
`pinsight_parse_nodepolicy(val)` → store into `domain_default_trace_config[d].nodepolicy[idx]`.
Adding a nodepolicy key is then *one DSL line* in a domain header — the parser needs no
change. (`[DOMAIN.global]` availability listings for `trace_mode`/nodepolicy are hardcoded
in the enum → ignored; no `[global]` parsing is added.) A `device_activity`/`measure` key in
a non-permitted section → warn + ignore (§3 scope enforced in one place).

**Energy `measure`** — the `[Energy]`/`[Power]` config struct (`energy_power_config_t`) is
still TODO in the energy plan, so for now we do **not** touch it. Basic support has two
inputs, both writing the single `energy_measure_policy` global in `energy.{h,c}` (§6.4):
1. **Env var `PINSIGHT_MEASURE_ENERGY`** = `ON|OFF|ANYONE_PER_NODE|LEADER_PER_NODE` — the
   immediate runtime control (energy has no config plumbing today).
2. **`[Energy] measure`** = `off|on|anyone_per_node|leader_per_node` — a minimal new
   `[Energy]` section handler (there is no existing one; energy is the first runtime energy
   config). Parsed via `pinsight_parse_nodepolicy()`.

`pinsight_parse_nodepolicy()` is **case-insensitive** so the uppercase env values and
lowercase config values share one parser. **Precedence: env overrides config** (env = the
explicit per-run override), config overrides the built-in default `ON`. When the full
energy config struct lands, the global folds into it (§7).

**Config + resolver helpers — in `trace_config.{h,c}`** (config setting/parsing lives
here, per #1):
```c
/* Parses "off|on|anyone_per_node|leader_per_node|rotate_per_node[:<ms>]" (case-insensitive).
 * rotate suffix -> {ROTATE, ms}; bare rotate -> {ROTATE, default_ms}; others -> {policy, 0}. */
pinsight_nodepolicy_val_t pinsight_parse_nodepolicy(const char *val, pinsight_nodepolicy_t dflt);
const char *pinsight_nodepolicy_str(pinsight_nodepolicy_t p);
int pinsight_get_nodepolicy_index(int domain_index, const char *key); /* name->index, parser/init only */

/* Does THIS process perform measurement <lockname> under <policy>? 1 = measure, 0 = skip.
 * Handles OFF/ON/ANYONE/LEADER; ROTATE is dispatched to the rotation subsystem (§6.10),
 * which reuses this per time-slice. Cached per <lockname> (incl. held flock fd). */
int pinsight_node_role(const char *lockname, pinsight_nodepolicy_t policy);
```
`pinsight_node_role` → `node_leader_status()` (env chain) + `claim_node_singleton()`
(flock) both in `trace_config.c`.

**MPI helper — in `pmpi_mpi.c`** (per #1, MPI code stays with the other PMPI code): the
`MPI_Init`/`MPI_Init_thread` wrappers call `MPI_Comm_split_type(SHARED)` and publish an
`extern int pinsight_mpi_local_rank` (−1 until set). `node_leader_status()` step 3 reads
that global — so `trace_config.c` makes **no** MPI calls.

`pinsight_node_role` logic:
```
OFF              -> 0
ON               -> 1
ANYONE_PER_NODE  -> claim_node_singleton(lockname)              /* flock; 1 winner/node */
LEADER_PER_NODE  -> r = node_leader_status();
                    LEADER -> 1 ; NON_LEADER -> 0 ; UNKNOWN -> claim_node_singleton(lockname)
```
`node_leader_status()` — the §4.3 chain (LEADER / NON_LEADER / UNKNOWN):
```
1. env PINSIGHT_NODE_LEADER set?  -> (atoi==1)?LEADER:NON_LEADER
2. local_rank_from_env() >= 0?    -> (==0)?LEADER:NON_LEADER
     chain: FLUX_TASK_LOCAL_ID, SLURM_LOCALID, OMPI_COMM_WORLD_LOCAL_RANK, MPI_LOCALRANKID
3. pinsight_mpi_local_rank >= 0?  -> (==0)?LEADER:NON_LEADER   /* global set by §6.5 MPI wrapper */
4. else                            -> UNKNOWN                   /* -> lock fallback */
```
`claim_node_singleton(name)` — `flock(LOCK_EX|LOCK_NB)` on
`/tmp/$USER/pinsight_<name>_singleton.lock`, hold fd for lifetime, cache (name,fd,won).
Reads only the `pinsight_mpi_local_rank` global (set by the MPI wrapper) — no MPI calls in
`trace_config.c`.

### 6.3 HIP / CUDA activity gate

`src/roctracer_callback.c` (and the CUPTI analog `src/cupti_callback.c`):
- Compute once, cached (index lookup per #2):
  ```c
  pinsight_nodepolicy_t p = domain_default_trace_config[HIP_domain_index]
                                .nodepolicy[HIP_NODEPOLICY_DEVICE_ACTIVITY];
  int hip_activity_measure = pinsight_node_role("hip_activity", p);
  ```
  (evaluate at `LTTNG_ROCTRACER_Init`; also usable at the lazy `apply_mode` pool-open path
  — same cached answer.)
- Replace `!hip_activity_pool_disabled()` at the **three** pool sites (init open+enable,
  `apply_mode` open, `apply_mode` enable — currently lines ~760/773/799) with
  `hip_activity_measure`.
- **Delete** `hip_activity_pool_disabled()` and the `PINSIGHT_HIP_DISABLE_ACTIVITY` env
  (§4.1/§5). Host callbacks (`hip_api_callbacks_enable`) stay ungated → host tracing on
  all ranks regardless.
- CUDA: same shape at the CUPTI `cuptiActivityEnable`/pool sites, keyed
  `nodepolicy[CUDA_NODEPOLICY_DEVICE_ACTIVITY]` → `pinsight_node_role("cuda_activity", …)`.

### 6.4 Energy gate

`src/energy.h` + `src/energy.c` + `src/enter_exit.c`:
- **One global in `energy.{h,c}`** holds the policy (no `energy_power_config_t` yet, per #3;
  needs the `pinsight_nodepolicy_t` enum → `energy.h` includes `trace_config.h`):
  ```c
  /* energy.h */ extern pinsight_nodepolicy_t energy_measure_policy;   /* default ON */
  ```
  Set by the minimal `[Energy] measure` parser (§6.2); the built-in default is `ON`.
- In `pinsight_energy_init()` (constructor path), after config parse:
  1. **Env override:** `if (getenv("PINSIGHT_MEASURE_ENERGY")) energy_measure_policy =
     pinsight_parse_nodepolicy(getenv("PINSIGHT_MEASURE_ENERGY"), energy_measure_policy);`
     (env wins over config; §6.2 precedence).
  2. `energy_measure = pinsight_node_role("energy", energy_measure_policy)` — store a
     single file-static flag.
- Early-return on `!energy_measure` in: `pinsight_energy_init` backend init (so only the
  measurer ever calls `amdsmi_init` → keeps the AMD-SMI teardown gotcha to one rank),
  `pinsight_energy_snapshot_enter/exit`, and the control-thread `pinsight_control_energy_sample`.
- This gate sits **above** the per-platform enables and socket/device masks — those apply
  unchanged inside a measuring process (when that config lands).

### 6.5 MPI leader confirmation (for the deferrable measurements)

`src/pmpi_mpi.c` MPI_Init / MPI_Init_thread wrappers, after `PMPI_Init`:
- `MPI_Comm_split_type(MPI_COMM_WORLD, MPI_COMM_TYPE_SHARED, 0, MPI_INFO_NULL, &node)`;
  `MPI_Comm_rank(node, &lr)`; store `pinsight_mpi_local_rank = lr` (global).
- Read by `node_leader_status()` step 3. Only affects processes that resolve *after*
  MPI_Init (power polling, lazy activity); the eager constructor decisions already used
  steps 1–2 or the lock fallback. `#ifdef PINSIGHT_MPI` guarded (pure-HIP builds skip it).
- **v1 may omit this** and rely on steps 1–2 + lock fallback; add it only if a
  launcher without a local-rank env var shows up.

### 6.6 Removal / migration

- Remove `PINSIGHT_HIP_DISABLE_ACTIVITY` (code + any docs).
- **Eval scripts** `eva/AMG2023-tuolumne/overhead/run_rep.sh` + configs currently encode
  the gate via that env (`hostonly`, `*_noact`, `mpihost_gate1`). Migrate to config:
  `device_activity = off` (was noact/hostonly), `= on` (was act), `= leader_per_node`
  (was gate1). Not shipped code, but do it so the reproducer keeps working.

### 6.7 File-change summary

| Action | File |
|---|---|
| **Modified** | `src/trace_config.h` — `pinsight_nodepolicy_t` enum; `nodepolicy_keys[]`/`num_nodepolicy_keys` in `domain_info`; `nodepolicy[]` in `domain_trace_config_t`; `measure` in `energy_power_config_t`; resolver decls |
| **Modified** | `src/trace_domain_dsl.h` — `TRACE_NODEPOLICY` macro |
| **Modified** | `src/trace_domain_HIP.h`, `src/trace_domain_CUDA.h` — `TRACE_IMPL_NODEPOLICY` + `TRACE_NODEPOLICY("device_activity", …)` |
| **Modified** | `src/trace_domain_loader.{h,c}` — `dsl_add_nodepolicy()` builder |
| **Modified** | `src/trace_config.c` — default-copy; `pinsight_parse_nodepolicy`/`_str`/`pinsight_get_nodepolicy`/`pinsight_node_role`/`claim_node_singleton`/`node_leader_status` |
| **Modified** | `src/trace_config_parse.c` — generic nodepolicy-key parse; minimal `[Energy] measure` handler |
| **Modified** | `src/roctracer_callback.c` (gate 3 sites, delete env), `src/cupti_callback.c` (CUDA analog) |
| **Modified** | `src/energy.h` (`energy_measure_policy` global decl), `src/energy.c`, `src/enter_exit.c` (energy gate) — `energy_power_config_t` NOT touched (§3/§7) |
| **Modified** | `src/pmpi_mpi.c` (optional MPI local-rank, §6.5) |
| **Modified** | eval `run_rep.sh` + configs (env → config migration) |
| **Docs** | README; energy plan §"Config Design" (`measure` key; retire the analysis-side dedup caveat) |

**No new source files** — the enum, structs, and resolver all live in existing
`trace_config.{h,c}`; the DSL keys in the existing domain headers.

Phases 2–3 (later) touch: `src/roctracer_callback.c` (Phase 2 reload trigger; read policy
live), `src/cupti_callback.c` (analog), `src/pinsight_control_thread.c` (Phase 3 rotation
tick, reusing the power-polling timer), `src/trace_config.{h,c}` (rotation selector helper).
Still no new files.

### 6.8 Test / validation plan

1. **Parser unit** (`test_config_parser`): all four values → correct enum in HIP/CUDA/
   Energy sections; rejected+warned in `[OpenMP.*]`/`[MPI.*]`.
2. **Single-node functional** (4 ranks/1 node): `device_activity = leader_per_node` →
   activity records on exactly 1 devId (was 4); `= on` → 4; `= off` → 0. Confirm host
   HIP events present on all 4 ranks in every case.
3. **Lock robustness:** `anyone_per_node` with 4 ranks → exactly 1 winner (grep devId);
   `kill -9` the winner mid-run → `flock` releases (no hang, no stale lock breaks a
   fresh job).
4. **Multi-node regression** (16×4): re-run the rank-gating experiment driven by
   `device_activity = leader_per_node` **config** (not the deleted env) → reproduce
   **+3.8%** solve overhead and the un-inflated `MPI_Allreduce` median (§8).
5. **Energy:** `measure = leader_per_node` on a multi-rank node → exactly one
   `energy_enter`/`energy_exit` pair per hostname; `= on` → one per rank (old behavior).
6. **Leader determinism:** with `PINSIGHT_NODE_LEADER` unset, confirm `FLUX_TASK_LOCAL_ID`
   picks local-rank-0 on **multiple** nodes (the open-item multi-node check).

---

### 6.9 Phase 2 — per-window activity toggle (host-on / activity-off per window)

Makes the `device_activity` *policy value* live (per-window) while keeping the *election*
per-run. Only `on↔off` toggles; the elected collector set never changes.

- **Election cached once (per-run); policy value read live.** Revises §6.3's "compute once,
  cached" to: `eligible(this_rank)` (from the cached election) is fixed; the on/off is read
  from `domain_default_trace_config[HIP].nodepolicy[idx].policy` at each transition.
- **Effective gate:** `should_collect = eligible AND (policy != off) AND trace_mode==TRACING`.
  Host callbacks stay ungated → host HIP tracing unaffected (this is the "host-on /
  activity-off" state `trace_mode` alone cannot express).
- **New reload trigger.** Today `apply_mode` fires on a *mode* change. A `device_activity`
  flip with no mode change (still `TRACING`, activity `on→off`) must also act: on config
  reload, if HIP is `TRACING` and the activity-enable state changed, call the existing
  pool open (alloc+enable) or close (flush+free) path. ~a dozen lines; the open/close code
  already exists in `apply_mode`.
- **Selection method pinned.** A reload that changes `anyone_per_node`↔`leader_per_node`
  (or `↔rotate`) is warned + ignored — no re-election, nothing hops (§4.5).
- **Overhead:** zero steady-state (checked at transitions, not per HIP call); one pool
  close/open per switch (≈ a mode transition); net *lower* than always-on since activity is
  skipped in the costly windows. Built on the **verified cyclic enable/disable** path.

### 6.10 Phase 3 — round-robin rotation (`rotate_per_node:<ms>`)

Rotate the single collector across the node's ranks over time → all N GPUs sampled at
1-collector overhead. Reuses Phase 2's toggle on a control-thread timer.

- **Time-derived selection, zero IPC.** Each rank computes the collector independently from
  the node-wide clock — no barrier, no shared memory, no per-rotation MPI:
  ```
  collector_local_rank(t) = floor(CLOCK_MONOTONIC(t) / T) mod N        /* T = period ms */
  this_rank_collects(t)   = (my_local_rank == collector_local_rank(t))
                            AND (trace_mode == TRACING)
  ```
  `CLOCK_MONOTONIC` is node-wide (one kernel/node), so all local ranks agree without
  communicating. Rotation is per-node independent (cross-node skew irrelevant).
- **Inputs:** `my_local_rank` (env / MPI split, §6.5); `N` = ranks/node from
  `MPI_Comm_split_type` **size** (rotation runs mid-run, post-`MPI_Init`; env
  `SLURM_NTASKS_PER_NODE` fallback); `T` from the `rotate_per_node:<ms>` param (default when
  omitted).
- **Control-thread rotation tick** (reuse the `sem_timedwait` timer shared with power
  polling): each tick recompute `this_rank_collects`; on change, open/close the pool via the
  Phase 2 path. The pool lifecycle is already control-thread-owned → no cross-thread races.
- **Boundary overlap is harmless.** Tick skew can leave *two* ranks collecting for <1 tick
  at a boundary — but overhead only appears at **≥3 collectors/node** (16×2 was +0.5%), so a
  brief 2-collector handoff never reaches the threshold. No guard band needed; pick
  `tick ≪ T`.
- **Overhead & benefit:** steady-state = `leader_per_node` (~+3.8%), just a different rank
  over time; one pool close+open per `T` (≈10 transitions over a 10 s solve at `T=1 s`);
  and it **spreads the +0.9% persistent-straggler residual** across ranks (plausibly
  *lower* than fixed `leader`).
- **Analysis caveat:** coverage is **time-sliced per GPU** (each GPU has records only during
  its 1/N slices). Fine for aggregate/statistical GPU behavior; a continuous per-GPU
  timeline has gaps. Analysis should tag each record's slice and compute per-GPU stats over
  covered intervals.
- **Proven substrate:** this is cyclic enable/disable on a timer — the cyclic path is
  already verified (no crash, `hipKernelActivity==hipKernelLaunch` per window).

---

## 7. Open items / future work

- **Fold `measure` into the planned energy config struct (#3) — DEFERRED.** For now only
  the *enum* is shared; energy holds `measure` in a **single global in `energy.{h,c}`**
  (§6.4), parsed by a minimal `[Energy] measure` handler. Rationale: the energy plan
  already calls for a **completely separate** `energy_power_config_t` (energy/power are very
  different from domains — no lexgions/modes/punits), and that struct + its `[Energy]`/
  `[Power]` section parser are still TODO. The nodepolicy `measure` field folds in cleanly
  when that struct is built — no need to design it now. (Energy is *not* a domain, so it
  does not use the `TRACE_NODEPOLICY` DSL; it just reuses `pinsight_nodepolicy_t` +
  `pinsight_parse_nodepolicy()` + `pinsight_node_role()`.)
- **Multi-node verification of `FLUX_TASK_LOCAL_ID`** (confirm it stays 0–3 per node,
  not global rank) before relying on the local-rank chain.
- **CUDA parity:** verify the CUPTI activity pool has an equivalent per-process open gate.
- **8 MB → 2 MB activity pool revert** (currently hardcoded 0x800000 from the
  flush-vs-collection probe) — orthogonal cleanup.

---

## 8. Appendix — evidence

**Rank-gating validation (16×4, flux `f3MpNPmqcpsR`, rep2/rep3 means):**

| arm | collectors/node | solve | vs notrace |
|---|---|---|---|
| notrace | — | 9.15 s | — |
| `mpihost_noact` | 0 | 9.42 s | +2.9% (host-tracing floor) |
| `mpihost_gate1` | 1 | 9.50 s | **+3.8%** |
| `mpihost_act` | 4 | 10.70 s | +16.9% |

**Jitter fingerprint (`MPI_Allreduce`, rep2):** per-call median — noact 740 µs, gate1
679 µs (≈ baseline, no inflation), act 5751 µs (~8×). Cross-rank CV — noact 0.52,
gate1 0.47, act 0.31.

**Density step:** 16×1 +1.0%, 16×2 +0.5%, 16×3 +12.2%, 16×4 +13.2% (±~3 pt run-to-run
scatter between allocations).

**References:** OS-noise amplification — Ferreira et al. (Sandia,
osti.gov/servlets/purl/1145507, /1424876); nonblocking-collective limits for AMG —
Widener/Levy/Ferreira/Hoefler IJHPCA 2016 (osti.gov/servlets/purl/1257977). ROCm
mitigations — rocprofv3 `--collection-period`
(rocm.docs.amd.com/projects/rocprofiler-sdk/en/docs-6.4.3/how-to/using-rocprofv3.html);
subset-rank practice (LUMI training).
