# WS1 — Manifest design: in-trace event + external sidecar

**Status:** detailed design · 2026-07-30 · refines WS1 in
`visualization_analysis_redesign.md` §4.

## 1. The question: event, or external script writing `run_manifest.json`?

**Answer: both — they are not alternatives.** Each mechanism can do things the
other structurally cannot, and the three requirements (one per trace + one per
MPI experiment; one per window/snapshot; info-maximal) split cleanly across
them.

What **only an in-trace event** can provide:

1. **Window/snapshot scoping (requirement 2).** A snapshot ring buffer is
   overwritten long before `snapshot record`; a rotation produces chunk files;
   a live viewer may attach late. An external file has no time coordinate
   inside the CTF stream — it cannot say which config/mode/binding was in
   effect *during this window*. Only a periodically re-emitted event (LTTng's
   own statedump pattern) lands a manifest inside every captured window.
2. **TraceCompass XML join.** XML state providers see only CTF events. A
   sidecar JSON is invisible to every XML analysis; there is no mechanism for
   an XML provider to read external files. If WS2 row labels or devId
   relabeling need manifest facts, those facts must be events.
3. **Runtime truth.** Only the process knows the env it *actually saw* vs
   launcher intent (the 2026-07-17 GPU-binding bug was exactly
   intent ≠ reality), the config as parsed, the domains actually initialized,
   its pid, and the authoritative post-`MPI_Init` rank.
4. **Deployment model.** An in-process tracepoint needs no launcher
   cooperation — preserves LD_PRELOAD-anywhere.

What **only an external file** can provide:

5. **Bulk and structure (requirement 3).** LTTng-UST events have a practical
   ~10-field ceiling (hit twice already in the CUPTI/roctracer headers), no
   nesting, and sequences are not addressable from TC XML. An lstopo XML
   export, a full environment dump, `amd-smi static`, a module list, `ldd`
   output are kilobytes to megabytes — they belong in files.
6. **Job-scope facts the process cannot know.** Campaign parameters
   (NODES/RANKS_PER_NODE/problem size), queue/bank, launch script revision,
   operator notes.
7. **Experiment scope (requirement 1).** One file at the run-folder root
   naturally describes the whole multi-node MPI experiment; no single traced
   process can write "the experiment's" record without election and races.

**Division rule:** *the event records what only the process can know plus the
in-stream join keys, kept tiny and re-emitted periodically; the sidecar
records everything else, as large as we like, written once per run by the
launcher.* The two are joined by directory layout and, explicitly, by a
shared `run_id`.

## 2. Artifact A — in-trace manifest burst (the PInsight source change)

### 2.1 Provider and events

New dedicated provider `pinsight_manifest_lttng_ust` (own provider like
energy, so sessions can enable/disable it independently:
`lttng enable-event -u 'pinsight_manifest_lttng_ust:*'`). Two event types,
both carrying the common `(hostname, pid)` fields:

```
manifest_process(seq: u64, reason: string, mpirank: int,
                 exe: string, window_gen: u32, nprocs_hint: int)
manifest_kv(seq: u64, key: string, value: string)
```

`window_gen` is the cyclic-window generation counter the control thread
already maintains (`ma->generation`) — it stamps every burst with the
producer-visible TRACING window it belongs to (see §2.6 on identity scopes).

A **manifest burst** = one `manifest_process` followed by N `manifest_kv`
events sharing the same `seq` (monotonic per process). Consumers treat
manifests as idempotent, **latest-wins per `(hostname, pid)`** — per the
shared-buffer association model in the redesign doc §4/WS1, nothing may rely
on stream identity or adjacency; the join is purely payload-level.

Rationale for the kv shape: a single fat event would hit the ~10-field
ceiling immediately and freeze the schema. `manifest_kv` gives unbounded
extensibility with two payload fields, and TC XML can store it generically
(`/manifest/<pid>/<key> = value` — field values are usable as attribute path
components), so **adding a new fact requires no schema, XML, or reader
change**.

### 2.2 Standard keys (initial dictionary — folds into metrics dictionary §5)

| Key | Source | Notes |
|---|---|---|
| `run_id` | see §2.5 | join key to sidecar and across nodes |
| `pinsight.version` | `PINSIGHT_VERSION` | plus git rev if cmake provides it |
| `pinsight.domains` | compiled + actually-initialized | e.g. `MPI,HIP,energy` |
| `pinsight.config_hash` | FNV-1a of the **effective in-memory config dump** (domains + knobs); cached, refreshed per config load, atomic-read per burst | join key to the sidecar config dump file (§2.8); reflects defaults/env/reload state, ignores comment-only edits |
| `launcher.rank` | `FLUX_TASK_RANK` / `PMI_RANK` / `SLURM_PROCID` | early best-effort |
| `launcher.job_id` | `FLUX_JOB_ID` / `SLURM_JOB_ID` | |
| `gpu.rocr_visible_devices` | env, raw string | likewise `hip.`/`cuda._visible_devices` |
| `gpu.physical_dev_offset` | `hip_visible_device_offset()` | the offset-correction actually applied |
| `cpu.affinity` | `sched_getaffinity` as range list | binding reality |
| `omp.num_threads` | env / runtime | |
| `host.exe` | `/proc/self/exe` | feeds WS12 symbolization |
| `host.exe_build_id` | ELF note of `/proc/self/exe` | per-process attribute, NOT part of any ID (§2.6); canonical key for WS12 binary matching |
| `host.cwd`, `host.cmdline` | `/proc/self/` | cmdline truncated |

Values capped (256 bytes, `…` marker on truncation). Every key optional —
emit what is knowable, never fail.

### 2.3 Emission points

1. **Constructor** (`enter_pinsight_func` in `enter_exit.c`, after config
   load) — `reason="init"`, rank best-effort from launcher env.
2. **After `PMPI_Comm_rank` in the `MPI_Init`/`MPI_Init_thread` wrappers**
   (`pmpi_mpi.c`) — `reason="mpi_init"`, authoritative rank.
3. **Periodic from the control thread** — `reason="periodic"`. Integrates
   into the existing min-deadline computation in `pinsight_control_loop`
   (alongside the rotation and window deadlines): compute the next manifest
   deadline, wait on the earliest, re-emit and loop. No new thread.
4. **After config reload and after every window/mode transition** —
   `reason="window"`; the control thread already owns both paths, and a
   reload may change config-derived kvs.
5. **At exit** (`exit_pinsight` path) — `reason="fini"`.

### 2.4 Config and overhead

Config keys live in a dedicated `[Manifest]` section (decided 2026-07-31),
NOT `[Domain.default]`: the manifest is not a tracing domain but a
process-global subsystem, and `[Domain.default]` keys participate in the
lexgion inheritance machinery (domain-default copies, per-lexgion overrides,
punit scoping) — none of which applies here. The parser precedent is the
existing `[Energy]` section (`measure` → plain global
`energy_measure_policy`); `[Manifest]` mirrors it with one plain global
(`manifest_interval_sec`), which the reload path re-parses — so `interval`
is runtime-adjustable via SIGUSR1 for free:

```
[Manifest]
interval = 10   # seconds; 0 = startup/transition-only (no periodic)
```

**There is deliberately no `emit = on|off` key (decided 2026-07-31).** The
on/off switch is session-level provider enablement
(`lttng enable-event -u 'pinsight_manifest_lttng_ust:*'`), which is uniform
for a whole session by construction. A config key that can flip mid-run
recreates the startup-only failure mode this design exists to fix (§1):
`emit` turned off by a reload leaves every later snapshot chunk without
bursts once the ring evicts the old ones — a half-self-describing trace.
Unlike `[Energy] measure` (which gates real AMD-SMI read cost), suppressing
manifest emission saves essentially nothing (~2 KB/process/interval; no-op
tracepoint branches when the provider is disabled in the session). Fewer
states for consumers to reason about, no footgun.

`interval = 0` carries a milder version of the same hazard: fine for batch
captures, wrong under snapshot/rotation modes (late chunks would lack
bursts). That stays guidance rather than enforcement — PInsight cannot see
external snapshot cadence.

**Dormancy rule (decided 2026-07-31)** — preserves the all-OFF ≈
zero-ambient-overhead contract:

- The manifest periodic timer arms **iff anything is being collected**:
  any domain mode ≠ OFF **or** `[Energy] measure ≠ off`. The energy term
  tests the configured **policy** (uniform across ranks), never the
  per-rank elected role — under `leader_per_node`, non-leader ranks emit
  manifests too; a role-based predicate would make manifest presence
  rank-dependent and break per-session uniformity. When all domains are
  OFF and energy is off, no manifest deadline is armed and the control
  thread stays in pure `sem_wait` (zero CPU) — ambient cost identical to
  pre-manifest PInsight. The control thread re-evaluates the predicate at
  every config reload and mode transition (it owns both paths), so late
  arming/disarming is free.
- **Energy is deliberately NOT under the dormancy rule.** It is an
  independent collection subsystem with its own switch (`[Energy]
  measure`, default **off** since 2026-07-31), because energy-only
  observation — all trace domains OFF, `measure ≠ off` — is a supported
  use case; the manifest still arms then, since energy data needs
  provenance (run_id, node identity, build/config) like any other trace
  data. Energy's own reconfig semantics (armed spans, disarm completes the
  measurement, variant latched per run) are energy-subsystem design, out
  of WS1 scope — see the 2026-07-31 amendment in
  `energy_power_implementation_plan.md`. WS1 only reads current state.
- Manifest **lifecycle** bursts (`init`/`mpi_init`/`window`/`fini`) remain
  unconditional — each is one tracepoint call (~ns when no session
  subscribes) and keeps even a dormant process identifiable if a session
  is recording. Only the periodic machinery is gated. Additionally,
  `pinsight_manifest_emit()` checks `lttng_ust_tracepoint_enabled()`
  before kv gathering, so an unsubscribed session skips even the
  `getenv`/`/proc` reads.

Resulting layer separation, each with one owner: **domain modes + [Energy]
measure** decide what is collected and whether ambient machinery runs;
**[Manifest] interval** tunes cadence when armed; **the LTTng session**
decides what is recorded.

Guidance: `manifest_interval` should be < the shortest capture window
(snapshot period, rotation period, `window_timeout`) — recommend half of it.
Cost: ~20 events × ~100 B per process per interval ≈ 200 B/s/process —
negligible against any tracing workload; emission from the control thread
adds zero app-thread perturbation for the periodic case.

### 2.5 `run_id` — the trace ID (decided 2026-07-30)

**Generated randomly *before* tracing starts, never derived from a post-run
content hash.** The build-id analogy actually argues for this: what makes ELF
build-id useful is that it is stamped into the artifact *at creation time* so
every later consumer finds it embedded — not that it is content-derived.
For traces, creation-time stamping is only possible with a pre-generated ID:

- The ID's primary job is a **join key** (events ↔ sidecar ↔ sibling node
  traces). Join keys must exist while events are being written; a post-hoc
  hash cannot be embedded in the stream, so the mapping would live in an
  external file — reintroducing exactly the fragility the ID is meant to
  remove.
- **"Trace completed" is ill-defined** in the capture modes we actually use:
  snapshot rings, rotation chunks, and live streaming have no single final
  byte set to hash; a windowed capture would change the trace's identity
  every window.
- **A content hash is not stable**: one experiment = N per-node CTF dirs
  (which dir's hash is "the" ID?), and any post-processing (chunk merging,
  re-indexing, metadata regeneration) changes bytes → the ID silently
  changes → provenance breaks. Content-addressing suits immutable
  self-contained artifacts; trace dirs are neither.

What a content hash *is* good for — integrity and dedup — stays available
without being the identity: the sidecar's post-run assembly step records
`sha256` per node dir in `run_manifest.json` (`nodes.<host>.trace_sha256`)
as an *attribute* of the run, not its name.

**Generation / propagation (fallback chain):**

1. **Launcher** (normal path): `pinsight-manifest.sh` derives
   `PINSIGHT_RUN_ID` from the scheduler job id when present (readable,
   already unique per site) plus a short random suffix, else `uuidgen`;
   writes it into `run_manifest.json` and exports it — all ranks inherit it.
2. **No launcher cooperation** (LD_PRELOAD-anywhere): each process generates
   16 random bytes (`getrandom(2)`) at the constructor and emits that as a
   provisional `run_id`. With MPI, the `MPI_Init` wrapper then **unifies**
   it: rank 0's ID is `PMPI_Bcast` (16 bytes, inside an already-collective
   call — negligible) and every rank's `reason="mpi_init"` burst re-emits
   the unified value. Latest-wins semantics make the provisional→unified
   transition invisible to consumers.
3. Non-MPI, no launcher: the per-process ID stands — still globally unique,
   just process-scoped, which is the correct scope for that deployment.

### 2.6 Identity scheme: scopes, windows, segments (decided 2026-07-30)

Considered and rejected: build-id as a run_id prefix (build-id is a
*per-process* fact — ill-defined at run scope under MPMD or Python; adds no
join power since joins need only uniqueness; composite keys assembled at
query time beat composite strings), and an in-band per-segment trace-id
(**segment boundaries are created by the capture machinery, not the traced
process** — `lttng snapshot record` / `lttng rotate` are external session
operations the producer never observes, so it cannot mint an ID for them).

"Trace-id" splits into two axes with different authoritative minters:

- **Producer-visible windows** (cyclic TRACING windows ended by
  count/timeout): PInsight knows these — `window_gen` on every
  `manifest_process` (the control thread's existing generation counter)
  stamps each burst with its window, fully in-band.
- **Capture-side segments** (snapshot/rotation chunks): identity stays
  out-of-band where it is already unambiguous — the chunk/snapshot
  directory name plus its time span. The in-band manifest `seq` values then
  state which part of the run a segment covers ("chunk contains bursts
  seq 17–19 of pid X"). When our own tooling creates the segment (the
  introspect script's snapshot path), it may additionally drop a
  `segment.json` sidecar `{run_id, hostname, window_gen, seq_span,
  sha256}` — nice to have, never required. (CTF metadata also carries a
  per-session `trace.uuid` minted by lttng-sessiond, linking chunks of one
  session out-of-band.)

Resulting scopes:

| Scope | Key | Where |
|---|---|---|
| experiment/run | `run_id` | in-band kv + sidecar |
| tracing window | `(run_id, hostname, pid, window_gen)` | in-band |
| capture segment | `(run_id, hostname, chunk_dirname)` | out-of-band; joined via contained `seq`/timestamps |
| node trace dir | `(run_id, hostname)` | layout + in-band |
| process | `(run_id, hostname, pid)` | in-band |

The binary's `host.exe_build_id` is an *attribute* of the process (kv +
sidecar `software` section), never a component of any ID.

### 2.7 User-provided facts (decided 2026-07-31 — noted for future reference)

Application/platform facts only the user knows (problem size, solver/
algorithm choice, campaign labels) are supported by two mechanisms; a third
was considered and rejected:

- **Sidecar (in scope, Step 4):** `pinsight-manifest.sh run --kv key=value`
  (repeatable) and/or merging a `user_manifest.json` found in the run dir →
  a `user:` section of `run_manifest.json`. Launcher-time facts, zero
  runtime involvement. Covers batch analysis and reports.
- **In-trace app-note API (follow-on after Step 1, not part of WS1):** the
  in-situ mirror of the existing app-knob API (`app_knob.h` is config→app;
  this is app→trace): `pinsight_note_kv/int/double(key, value)` emits the
  existing `manifest_kv` event with an `app.` key prefix — latest-wins,
  window-scoped, TC-visible, no new event type or consumer logic. Deployed
  as a header-only dlsym shim that no-ops when PInsight is not preloaded
  (the NVTX pattern; zero link dependency). Scope guard: this API is for
  **facts** (latest value = the meaning); *time-varying* values to plot
  (residual, timestep) are a sample series and belong to a WS11b-family
  event, never the manifest. First validation use case: stamping AMG
  problem size + solver into campaign traces.
- **Rejected:** user kvs in `[Manifest]` config (literal or
  script-valued `$(cmd)`), and `PINSIGHT_KV_*` env auto-emission — no
  concrete use case; script evaluation brings subprocess lifecycle/
  timeout/caching complexity; the two mechanisms above cover the named
  needs with less machinery. Revisit only with a use case neither covers.

### 2.8 Effective-config dump files (decided 2026-07-31)

The full config **content** is bulk, so per the §1 division rule it is NOT
an in-event record (a `manifest_config` blob event was considered and
rejected). Instead:

- Every burst carries `pinsight.config_hash` = FNV-1a over the
  **round-trippable in-memory dump** (`print_domain_trace_config` +
  `pinsight_print_knob_config` via `open_memstream` — pure formatting, no
  file I/O). **Cached-buffer mechanism (decided 2026-07-31, implemented;
  ownership moved to `trace_config.{h,c}` same day):** the cache (buffer +
  atomic `uint64` hash) belongs to the config subsystem —
  `pinsight_config_dump_refresh()` runs **automatically at the end of every
  `pinsight_load_trace_config`** (initial load, reloads, no-file/defaults
  case), so no caller ever wires a refresh. SWMR: the buffer belongs to the
  load callers (constructor / control thread, never concurrent); any thread
  reads the hash via `pinsight_config_hash_get()` (`__atomic_load`,
  relaxed — a plain mov), so burst/app threads do zero formatting and no
  locking exists. The Step 3 file flush writes exactly this cached buffer
  (`pinsight_config_dump_get()`, writer-side only), making hash↔file
  consistent by construction.
  This is the *effective* config: defaults applied, env overrides, reload
  state; comment-only file edits don't change it. The earlier
  `pinsight.config_path` key was dropped along with everything else
  config-file-related (2026-07-31): with the hash keyed to the effective
  state and the content in dump files, the on-disk path is redundant and
  potentially misleading (the file can change after parse).
- The **control thread writes the content** to
  `$PINSIGHT_MANIFEST_DIR/pinsight_config.<hash>.txt` at control-thread
  start and after each reload (Step 3). Content-addressed → idempotent and
  deduped: write only if absent (tmp + rename), so N ranks/node produce
  one file per distinct config and a run with two reloads at most three
  small files. `PINSIGHT_MANIFEST_DIR` is set by the launcher
  (`pinsight-manifest.sh`); **unset → no dump** (LD_PRELOAD-anywhere, no
  surprise files; the hash stays in the trace regardless).
- The **sidecar packs** `pinsight_config.*.txt` into `manifest/` and lists
  them in `run_manifest.json` (Step 4). Consumer story: burst's
  `config_hash` → `manifest/pinsight_config.<hash>.txt`.
- Accepted trade-off: a bare trace dir shipped without its run folder
  loses the config *content* (keeps the hash) — the standard sidecar
  trade-off.

**Granularity / nodepolicy (clarified 2026-07-31):** the in-trace manifest
is **per rank, always, no nodepolicy** — its facts (rank, pid, affinity,
visible-devices as seen, cmdline) differ per rank, so any
`leader_per_node`-style reduction would destroy exactly the information it
exists to capture (contrast energy, whose node-wide *duplicate* values make
singleton election lossless). The sidecar is **per node (hardware) + per
run (job)**. The config dump files sit between and get per-node dedupe for
free from content addressing (first-writer-wins ≈ `anyone_per_node` with
no election machinery); ranks with genuinely different effective configs
produce different hashes → different files, each rank's bursts pointing to
its own — correct, not a conflict.

## 3. Artifact B — `run_manifest.json` sidecar (script, no PInsight change)

Written by a new collector script `scripts/pinsight-manifest.sh` invoked by
the launch wrappers; **one per experiment**, at the run-folder root next to
the per-node CTF dirs. Bulk blobs go in an adjacent `manifest/` directory,
referenced from the JSON, keeping the JSON greppable:

```
<run_dir>/
  run_manifest.json
  manifest/
    lstopo.<host>.xml        env.<host>.txt        amd-smi.<host>.txt
  <node1-trace>/ ... <nodeN-trace>/
```

Schema (all fields optional; `schema_version` at top):

- `run`: `run_id`, start/end timestamps, launcher command line, campaign
  parameters (NODES, RANKS_PER_NODE, problem size), operator notes.
- `job`: scheduler (flux/slurm), job id, queue/bank, node list.
- `software`: PInsight git rev + build options, app binary path + sha256 +
  `ldd` ref, module list, ROCm/CUDA/MPI versions.
- `nodes.<hostname>`: lstopo ref, CPU model, memory, GPU inventory
  (`amd-smi static` ref), kernel version; post-run `trace_sha256` of that
  node's trace output (per rotation chunk when applicable — chunks are the
  natural integrity unit there, and it keeps any `segment.json` consistent
  with the run-level record).
- `intent`: intended binding/GPU map per rank — cross-validated by the
  report layer against the in-trace `gpu.*`/`cpu.affinity` kvs (mechanized
  detector for the binding-bug class: intent ≠ reality → flag).

Per-node collection: one `flux exec`/`srun` pass at job start (tolerate
partial failure). `run_id`: the launcher takes the scheduler job id (else a
uuid), writes it into the JSON, and exports `PINSIGHT_RUN_ID` so the traces
echo it — the trace↔sidecar join survives directory reshuffling.

Consumers: **Python only** (WS5 report, `pinsight_reader` convenience
loader). TC never requires it.

## 4. Requirement mapping

| Requirement | Mechanism |
|---|---|
| One per trace (per-node CTF dir) | in-trace bursts cover every process in that dir; self-contained |
| One per MPI experiment | `run_manifest.json` at run root; in-trace `run_id` joins traces to it (and TC experiments resolve members as today) |
| One per window/snapshot | periodic re-emission (`manifest_interval` < window) + re-emit at every mode/window transition ⇒ ≥1 burst per live process in every snapshot/rotation chunk |
| Info-maximal (hw/sw/topology/binding) | sidecar carries the bulk (lstopo, env, versions, inventory); event carries runtime truth + join keys |

## 5. Consumers

- **`pinsight_reader.py`:** `manifests(traces)` → `{(hostname,pid): {kv…}}`
  latest-wins, plus an as-of-timestamp variant for windowed queries;
  `load_run_manifest(run_dir)` for the sidecar. First user:
  `mpi_gpu_energy_report.py` replaces its one-GPU-per-rank inference with
  recorded facts.
- **TC XML (WS2):** the combined state provider stores `manifest_kv` under
  `/manifest/<pid>/<key>` and resolves via `query`-type attributes (kernel
  "current thread on CPU" pattern) — row labels, devId relabeling.
- **WS5 report:** manifest summary section; intent-vs-reality validation.
- **WS7 Perfetto:** track names/metadata from the same reader dict.

## 6. Non-goals (unchanged from the redesign doc)

Not needed for Host→Rank timeline grouping (per-event hostname/mpirank cover
that); does not resolve ROCm activity-record devId numbering
(driver-internal; stays inferential); not a hardware-counter time series
(WS11b).

## 7. Implementation plan (expanded 2026-07-30)

Decisions locked before coding: `run_id` = random-at-start with MPI_Init
unification (§2.5); identity scheme incl. `window_gen` + out-of-band segment
identity, build-id as attribute (§2.6); `[Manifest]` section with `interval`
only, no on/off key; dormancy rule — manifest periodic arms iff any domain
≠ OFF or energy measure on, energy independent with its own switch (§2.4);
kv value cap 256 bytes; latest-wins **per key** in consumers (§8
burst-atomicity point — adopted).

### Step 1 — provider + core emitter (~½ day) — **DONE 2026-07-31**

Implemented as designed (`manifest_lttng_ust_tracepoint.h`, `manifest.c/.h`,
CMake unconditional) plus the constructor/`fini` hooks from Step 2 so it is
testable end-to-end. Validated on Tuolumne with a live session enabling only
`pinsight_manifest_lttng_ust:*`: init + fini bursts (seq 0/1) carry all
knowable keys (run_id from env, version, domains, affinity,
omp.num_threads, exe, exe_build_id — verified byte-identical to
`readelf -n` — cwd, cmdline); unknowable keys correctly skipped
(launcher.*/gpu.* with no launcher); no-session preload is silent
(tracepoint_enabled guard). `config_hash` repointed same day to the
effective in-memory dump (§2.8) — verified always present, stable across a
process's bursts, and different between a default-config and a
config-file process. Remaining from Step 2: MPI_Init run_id unification +
authoritative-rank burst.

- `src/manifest_lttng_ust_tracepoint.h`: provider
  `pinsight_manifest_lttng_ust`, events `manifest_process` / `manifest_kv`
  per §2.1, using `COMMON_LTTNG_UST_TP_FIELDS_GLOBAL`; modeled on
  `energy_lttng_ust_tracepoint.h`.
- `src/manifest.c` (+`.h`): the `LTTNG_UST_TRACEPOINT_DEFINE` unit for the
  provider (same pattern as every other provider .c). API:
  - `void pinsight_manifest_init(void)` — cache immutable facts once
    (exe path, version string, domains, config path/hash, provisional
    `run_id` via env → `getrandom`).
  - `void pinsight_manifest_emit(const char *reason)` — atomic `seq++`,
    emit `manifest_process` + kv burst (env echoes, `sched_getaffinity`,
    `hip_visible_device_offset` where compiled, launcher vars). Never
    fails; skips unknowable keys.
  - `void pinsight_manifest_set_run_id(const char id[33])` — called by the
    MPI unification path.
- CMake: add both files to the same source lists that carry
  `enter_exit.c` (unconditional — manifest has no backend deps).
- **Test:** single-process smoke; `babeltrace2 | grep manifest_` shows one
  burst with expected keys; kv values truncated at 256 B.

### Step 2 — emission hooks (~½ day)

- `enter_exit.c`: `pinsight_manifest_init()` + `emit("init")` in the
  constructor after config load, before the `enter_pinsight` marker;
  `emit("fini")` next to the `exit_pinsight` marker (plain tracepoint —
  no energy-style teardown hazard).
- `pmpi_mpi.c`: in both `MPI_Init` / `MPI_Init_thread` wrappers after
  `PMPI_Comm_rank`: `PMPI_Bcast` rank 0's 16-byte `run_id` over
  `MPI_COMM_WORLD`, `pinsight_manifest_set_run_id()`, `emit("mpi_init")`.
- **Test:** 2-rank MPI smoke — both ranks' `mpi_init` bursts carry rank 0's
  `run_id` and authoritative `mpirank`.

### Step 3 — periodic + transition re-emit (~½–1 day)

- `trace_config_parse.c` / `trace_config.h`: new `[Manifest]` section
  (§2.4) modeled on `SECTION_ENERGY`: `interval = <sec>` (default 10;
  0 = no periodic) → plain global `manifest_interval_sec` next to
  `energy_measure_policy`. No on/off key — session-level provider
  enablement is the switch (§2.4).
- `pinsight_control_thread.c`: manifest deadline joins the existing
  min-deadline selection (rotation / window / manifest — wait on earliest);
  on expiry `emit("periodic")`, recompute. `emit("window")` after the
  config-reload handler and after the mode-change/window-end handler
  (config-derived kvs may have changed). Arm the deadline only per the
  dormancy rule (§2.4: any domain ≠ OFF or energy measure on);
  re-evaluate after every reload/transition.
- Effective-config dump flush (§2.8): the cache refresh is already
  automatic (inside `pinsight_load_trace_config`), so the reload handler
  only adds the FLUSH — write `pinsight_config_dump_get()`'s buffer to
  `$PINSIGHT_MANIFEST_DIR/pinsight_config.<hash>.txt` if absent
  (tmp + rename), then emit the `window` burst — so the file exists
  before any burst references its hash. Same flush once at control-thread
  start. Env unset → flush returns immediately: zero file I/O even across
  reloads (the refresh itself is pure in-memory formatting).
- **Test:** snapshot-mode run with `manifest_interval` < snapshot period —
  every snapshot chunk contains ≥1 burst per live process (the WS1
  acceptance property); cyclic-window run (`test/rocm/cyclic_traces`
  recipe) shows `reason="window"` bursts at each transition.

### Step 4 — sidecar collector (~1 day)

- `scripts/pinsight-manifest.sh`, two subcommands:
  - `node <outdir>` — per-node collection (lstopo XML, `amd-smi static`,
    cpu model/mem/kernel, env dump) into `manifest/*.<host>.*`; every
    field optional, partial failure tolerated.
  - `run <rundir>` — generate `run_id` (§2.5), assemble
    `run_manifest.json` (schema §3), export `PINSIGHT_RUN_ID` and
    `PINSIGHT_MANIFEST_DIR` (§2.8); accept `--kv key=value` / merge
    `user_manifest.json` into the `user:` section (§2.7); post-run
    invocation adds `trace_sha256` (per node dir; per chunk on
    rotation-mode runs, §3) and packs `pinsight_config.*.txt` into
    `manifest/` with references in `run_manifest.json`.
- Wire into the pinsight-eval launch harness (one `flux exec` pass at job
  start); session scripts add
  `lttng enable-event -u 'pinsight_manifest_lttng_ust:*'`.
- Optional: `segment` subcommand emitting `segment.json` (§2.6) from the
  introspect-script snapshot path.
- **Test:** 4-node AMG run → sidecar + blobs present; traces echo the
  sidecar's `run_id`.

### Step 5 — consumers (~1–2 days)

- `analysis/pinsight_reader.py`: `manifests(traces)` → latest-wins-per-key
  `{(hostname,pid): {...}}` + as-of-timestamp variant;
  `load_run_manifest(run_dir)`.
- `mpi_gpu_energy_report.py`: replace the one-GPU-per-rank inference with
  manifest facts — the Step-5 acceptance test.
- `analysis/tc/pinsight_analysis.xml`: `manifest_kv` handler →
  `/manifest/<pid>/<key>` (minimal now; WS2 consumes it for labels/devId).
- Docs: `analysis/README.md` row; redesign doc WS1 marked done.

Overall acceptance (P1 tie-in): a 4-node experiment yields traces + sidecar
sharing one `run_id`; every snapshot window is self-describing; the report
layer can mechanically flag binding intent ≠ reality.

Sequencing: src tree is clean as of `005e9ff` (cupti/roctracer changes
committed); the node-singleton control-thread work landed earlier — no
entanglement. Branch: `ws1-manifest`.

## 8. Open questions

- Exact value cap (256 B proposed) and whether `host.cmdline` is worth the
  truncation noise.
- Burst atomicity: a snapshot can slice a burst mid-way; consumers must
  tolerate a partial newest burst by falling back to the previous complete
  `seq` per key (latest-wins per key, not per burst, makes this a non-issue —
  adopt that reading in the reader).
