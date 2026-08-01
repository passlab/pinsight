# PInsight visualization & analysis redesign

**Status:** high-level design · 2026-07-17 · progress updates 2026-07-24 and
**2026-07-29** (WS9 done; toolkit now lives in `pinsight/analysis/`; WS10–WS12
added from the flow-graph / efficiency-graph / source-exploration
consolidation; prior 3D + source-lookup experiments recorded in §7 — snapshot
at end of §6)
**Decisions (fixed 2026-07-17, amended 2026-07-29):** TraceCompass
**XML-first** stands for everything XML can express (timeline, segments, XY
views). Two amendments: (1) a **Java plugin is conditionally in scope** for
the two things XML structurally cannot express — time-graph **arrows** (WS10)
and codeptr **source lookup** (WS12) — gated on the Perfetto validation in
WS10; `pinsight-tracecompass` (repurposed 2026-07-27, `2c48725`) is its home,
and the prebuilt trace-compass-server in this repo checkout is the headless
deployment target. (2) The PInsight source-change budget widens from "one
manifest event" to three small, independently phased items: manifest (WS1),
MPI `bytes` + communicator fields (WS10), periodic counter/energy sample
(WS11b). **All domains** (OpenMP, MPI, HIP, CUDA, Python, energy) brought to
the same level, not just the AMG/MI300A path — unchanged.

## 1. Current assets (three disconnected layers)

| Layer | What exists | State |
|---|---|---|
| Trace data | 7 tracepoint domains; PMPI carries `count/dest/source/tag`; HIP host callbacks **and** GPU activity records (`correlation_id`, GPU-side `begin/end_ns`); energy enter/exit brackets (node-wide counters); multi-node = per-node CTF dirs | Richer than any view exploits |
| TraceCompass integration (`analysis/tc/`) | Two XMLs — `pinsight_analysis.xml`: ONE combined state provider + time-graph view (per the 2026-06-29 decision: cross-domain correlation on a shared time axis requires a single provider/view; location trees: Processor, OS/OMP Thread, OMP Team/Region, MPI Rank, HIP Device/Kernel, CUDA Device/Kernel) and `pinsight_omp_pattern_analysis.xml` (FSM segment analysis, **OpenMP-only, stale**) — plus the WS9 LAMI bridge: `lami.py`, generic `lami_adapter.py`, `resolve_experiment.py`, `tc-common.sh`, `tc-setup.sh`, `tc-local.conf(.example)`, and four committed wrappers (`lami_{load_imbalance,mpi_latency,halo_exchange,gpu_datamovement}.sh`) | XML working but shallow; activity records excluded; no `<xyView>`, no energy/pysysmon handlers |
| Python (`analysis/`, formerly `peam/src/`) | `pinsight_reader.py` (shared reader) + load_imbalance / mpi_latency / mpi_jitter / gpu_datamovement / halo_exchange / mpi_gpu_energy_report / parse_energy / phase_detect (2026-07-27: change-point phase segmentation + iteration periodicity from 10 ms binned rates) | Validated on the 4-node run; since 2026-07-20 each script declares the neutral table contract (text/`--json`/`--csv`); `analysis/README.md` is the user guide. `phase_detect` committed with wrapper + README row 2026-07-29 (`f6e9c92`). Gaps: `mpi_jitter` documented but has no `tc/lami_*.sh` wrapper; `mpi_gpu_energy_report` still reads babeltrace2 on stdin and bypasses `pinsight_reader` |
| trace-compass-server (repo checkout, untracked) | Prebuilt TC 10.2 + incubator 0.18 trace server (headless: no UI/EASE plugins; incubator rocm.core/gpu.core present but PInsight-irrelevant) | Started 2026-07-07/08 but never fed a trace or our XML — WS8 pilot not exercised in substance |

Path note: peam was restructured 2026-07-20 (commit `cfcbfa5`) —
`src/python/` flattened to `src/`, and the XML analyses folded into
`src/tc/` so ALL TraceCompass material (LAMI bridge + wrappers +
`tc-setup.sh` + XMLs) lives in one place. On 2026-07-27 the toolkit moved
into the pinsight repo as `analysis/` (three-repo split; `peam/src` →
`pinsight/analysis`, `peam/src/tc` → `pinsight/analysis/tc`). Older path
mentions below are kept as-written where they name a commit-time layout.
User-facing usage documentation lives in `analysis/README.md`; this doc
holds the design and implementation record.

Plus TC built-ins (event table, count-based statistics pie, histogram) and the
validated capture workflows (batch fetch, snapshot import, live; see
trace_streaming_design.md §13).

## 2. Pain points driving the redesign

1. **Timeline renders idleness loudest.** `HIP_STATE_FREE` / `MPI_STATE_NOT_IN_MPI`
   / `PINSIGHT_STATE_STARTED` dominate in saturated colors; in-MPI/kernel-running
   spans are slivers. States were named/colored for correctness debugging, not
   analysis.
2. **Cryptic row identity.** Rows are bare thread ids / CPU ids / `4000`-series
   HIP thread ids with no Host → Rank grouping; a 4-node 16-rank experiment
   shows no node structure; every trace is named `64-bit`.
3. **Best data absent interactively.** GPU activity records (true kernel exec,
   launch→exec correlation) live only in the Python scripts. Host devId (0-3,
   physical after the 2026-07-17 fix) vs activity devId (4-7, ROCm driver
   numbering) still unreconciled.
4. **Statistics = raw event counts** (pie chart). No time-in-state, no duration
   distributions — nothing the Python side computes is visible interactively.
5. **Pattern/segment analysis OpenMP-only + stale.** TC's segment-store gives
   duration statistics/density/scatter views free once segments are defined —
   unused for MPI/HIP/CUDA/Python.
6. **Two toolchains, zero shared definitions.** XML and Python each define wait
   categories/state names/metrics; nothing keeps them consistent.
7. **"Lost Events"** visible in views — channel overruns during capture
   silently under-count every analysis.
8. Energy invisible interactively (bracket-only; deferred: no time series until
   an `energy_sample` is implemented — out of scope this round).
9. *(added 2026-07-29)* **No causality/flow view.** Nothing draws MPI
   send→recv, kernel launch→execution, or OMP fork/join relationships; the
   timeline shows rows in isolation → WS10.
10. *(added 2026-07-29)* **No resource-efficiency view — and no data for
    one.** No CPU/memory/network hardware counters are traced at all → WS11.
11. *(added 2026-07-29)* **codeptr fields are opaque.** `parallel_codeptr` /
    `mpi_codeptr` / `cuda_codeptr` identify lexgions everywhere (XML rows,
    scripts, reports) but nothing resolves them to `file:line` → WS12.

## 3. Target architecture

```
             ┌──────────────────────────────────────────────────────┐
             │  PInsight trace (CTF)                                │
             │  + NEW: manifest event (once per rank at startup)    │
             └──────────────┬───────────────────────┬───────────────┘
        ┌───────────────────┴────────┐   ┌──────────┴──────────────┐
        │ TraceCompass (interactive) │   │ Python (batch/reports)  │
        │  unified timeline (1 SP)   │   │ pinsight_reader + N     │
        │  per-domain pattern XMLs   │   │ analysis scripts        │
        │  time-in-state XY views    │   │ + HTML run report       │
        └───────────────────┬────────┘   └──────────┬──────────────┘
                            └────────┬──────────────┘
                     ┌───────────────┴───────────────┐
                     │ METRICS DICTIONARY (one doc)  │
                     │ states, categories, colors,   │
                     │ metric definitions — both     │
                     │ implementations conform to it │
                     └───────────────────────────────┘
```

Roles: **TraceCompass** = interactive exploration (timelines, segment
statistics, live/snapshot). **Python** = scriptable batch analysis, regression
comparisons, publication figures, one-command run reports. **Metrics
dictionary** = the contract that keeps them saying the same thing.

## 4. Workstreams

### WS1 — Manifest event (the one PInsight source change)
**Detailed design: `ws1_manifest_design.md` (2026-07-30)** — resolves the
event-vs-external-file question as *both* (dedicated
`pinsight_manifest_lttng_ust` provider emitting `manifest_process` +
`manifest_kv` bursts, plus a launcher-written `run_manifest.json` sidecar
joined by `run_id`). The sketch below is the original outline.

One tracepoint, e.g. `pinsight_enter_exit_lttng_ust:manifest`, carrying:
- identity: `mpirank` (early-env best-effort at startup, authoritative after
  MPI_Init), `pid`, `hostname`, launcher vars (`FLUX_TASK_RANK`/`PMI_RANK`)
- GPU map: `ROCR_VISIBLE_DEVICES`/`HIP_VISIBLE_DEVICES` raw string + the
  offset-corrected physical devId this process reports
- build/config: PInsight version-ish string, enabled domains, trace-config path

**Association model (why shared per-UID buffers are not a problem):** all of a
node's processes share ring buffers/streams, so nothing may rely on stream
identity or event adjacency. The manifest is joined to other events purely at
the **payload level**: every PInsight event already carries `(hostname, pid)`
in its common fields (self-containment), and the manifest is a
`(hostname, pid) -> metadata` record. Python = a dict; TC XML = store manifest
fields keyed by pid in the state system and resolve via `query`-type state
attributes (same mechanism as kernel "current thread on CPU" lookups).
Events remain self-contained for high-frequency fields (mpirank, devId); the
manifest carries only low-frequency once-per-process facts that would be
wasteful on every event.

**Must be periodic, not once-only.** A startup-only manifest is lost to every
windowed capture mode we actually use: snapshot (ring overwritten long before
`snapshot record`), rotation (only chunk #1 would have it), live with a
late-attaching viewer. Fix mirrors LTTng's own statedump pattern: the control
thread re-emits the manifest every few seconds and at window/mode transitions
(one tiny event per process per interval — negligible). Consumers treat
manifests as idempotent, latest-wins per `(hostname, pid)`.

**Who produces it — PInsight, with an optional launcher sidecar.** The
in-trace event must come from PInsight: only the process knows the runtime
truth about itself (env actually seen vs launcher intent — the 2026-07-17
GPU-binding bug was exactly intent≠reality; pid; config as parsed; domains
actually initialized; authoritative post-MPI_Init rank), only an in-process
tracepoint lands in the CTF stream where TC XML can see it and join it by
(hostname,pid), and requiring no launcher cooperation preserves the
LD_PRELOAD-anywhere deployment model. Job-level facts mostly reach the
process as inherited env vars (FLUX_JOB_ID, PMI_RANK, ...) and are recorded
from there. Separately, launcher scripts formalize what they already drop
next to the trace output (job.sh, flux.out) into a small `run_manifest.json`
(campaign parameters: NODES/RANKS_PER_NODE/problem size/queue/bank/script
rev) — consumed only by the Python report layer (WS5), never required by TC.

**Purpose (scoped honestly):** WS1 is a provenance + launch-environment
record — it makes traces self-describing (which build/config produced this
run; six near-identical trace dirs from one week are otherwise
indistinguishable), puts per-process GPU binding in the trace (the binding
bug needed a dedicated diagnostic cluster job to discover), and replaces
analysis-side heuristics (e.g. the one-GPU-per-rank inference in
mpi_gpu_energy_report.py, which misfired on a stray mpirank=0 artifact) with
recorded facts. It is NOT needed for Host→Rank timeline grouping (per-event
hostname/mpirank already cover that) and does not resolve the ROCm
activity-record devId numbering (driver-internal; stays inferential).

### WS2 — Unified timeline restructure (`pinsight_analysis.xml`)
- **Entry hierarchy:** Host → MPI Rank → {MPI state row, GPU device row(s),
  OMP team/thread rows, Python row} (manifest + hostname field make this
  derivable). Keep ONE state provider (unchanged decision).
- **Idle de-emphasis:** dim/near-transparent colors for FREE/NOT_IN/STARTED
  states; saturated colors reserved for active/wait states per the dictionary.
- **devId reconciliation:** display physical devId everywhere (host events
  already physical post-fix; activity rows relabeled via manifest mapping).
- **Rename** analysis/views: "PInsight Unified Timeline" (drop "OpenMP and
  CUDA"); give per-domain sub-views meaningful names.
- All-domain parity: CUDA rows get the same treatment as HIP; Python
  (pysysmon) events get a per-rank row.

### WS3 — Per-domain pattern/segment analyses (the big interactive win)
Separate XML files per domain (pattern analyses compose fine, unlike the
single timeline provider): each defines segments → TC gives Latency
Statistics / Density / Scatter / Table views automatically.
- `pinsight_mpi_pattern_analysis.xml`: segment per MPI call (name, rank,
  count, peer). Interactive equivalent of mpi_latency.py distributions.
- `pinsight_hip_pattern_analysis.xml`: segments for host calls; **stretch:**
  launch→activity correlation via stored `correlation_id` in FSM state (XML
  can store/test fields; feasibility risk — see §6. If XML can't, that one
  segment type stays Python-only rather than pulling in Java).
- `pinsight_omp_pattern_analysis.xml`: refresh to current tracepoints/fields;
  keep parallel-region segments (team_size etc.).
- `pinsight_cuda_pattern_analysis.xml`, `pinsight_python_pattern_analysis.xml`:
  same pattern for CUDA calls and Python function/monitoring spans.

### WS4 — Statistics that mean something
- Time-in-state per rank/domain via XML XY views (e.g. cumulative MPI-wait
  per rank over time; kernels in flight per device) to replace event-count
  pies as the first-look statistics.
- Segment-store statistics from WS3 cover duration distributions.
- *(2026-07-29)* Expanded into WS11a — same mechanism, with the metric set
  seeded from `phase_detect.py`'s per-bin features so XML and Python agree.

### WS5 — Python side: conform + report
- Align state/category names and colors to the metrics dictionary (small
  renames in the four scripts).
- `make_report.py`: one command per run → single HTML (manifest summary, the
  four analyses' tables, figures, links). Batch counterpart of the TC views.

### WS6 — Capture hygiene (no code)
- Session recipe: size `--subbuf-size`/`--num-subbuf` so "Lost Events"
  disappears at AMG-scale rates; document in the launch scripts. Analyses
  should also print discarded-event counts when babeltrace2 reports them.

### WS7 — Perfetto exporter (web-UI hedge #1, low risk)
`to_perfetto.py` on top of `pinsight_reader.py`: convert a (multi-node)
PInsight trace to Perfetto's format. Mapping: rank → process track,
thread/device → thread tracks, begin/end pairs → slices, **`correlation_id`
launch→exec and MPI send→recv pairs → flow arrows**, energy → counter
tracks, manifest → track names/metadata. Adds a polished, shareable,
SQL-queryable web viewer (ui.perfetto.dev or self-hosted) alongside TC —
replaces nothing, no live mode (snapshot/batch feed it). This is the
highest-UX-per-effort item in the whole plan.
*(2026-07-29)* Elevated to a near-term action: it now also serves as the
**validation gate for WS10** — arrows are cheap here, so we learn whether a
flow view is readable at AMG scale before committing to any Java.

### WS8 — Trace-server + VS Code frontend pilot (web-UI hedge #2, timeboxed)
The TC **trace server is the same Java core** — our XML analyses load
unchanged and are served over TSP to the theia/VS Code trace extensions. So
the XML investment is frontend-portable by construction; the open question is
only frontend maturity. Pilot (timeboxed, ~a day): run trace-compass-server
(dir already present in the repo checkout) against `pinsight_analysis.xml` +
a real 4-node trace, drive from the VS Code trace extension, and write down
concretely what works / what's missing vs Eclipse (XML management, time-graph
fidelity, filtering, live). Decision input for whether/when the primary
frontend shifts to web — replaces speculation with evidence. Custom
from-scratch timeline web UIs stay out of scope regardless (Perfetto covers
the polished-viewer need; custom web effort is confined to the WS5 report).

### WS9 — GUI ↔ Python bridge: run the analysis scripts from TraceCompass
Goal: right-click a trace/experiment in TC → menu item runs one of our Python
analyses → results render as an Eclipse view. Two mechanisms, in order:
- **LAMI "External Analyses" (spike first, zero Eclipse coding).** TC's
  built-in external-analysis framework (the "External Analyses"/"Reports"
  nodes already visible in the project tree) runs a configured command,
  passing the trace path and — key feature — the currently selected time
  range (`--begin/--end`), expects LAMI-1.0 JSON (`--metadata` handshake +
  results), and renders tables + basic charts persisted under Reports.
  Limits: tables/simple charts only, framework is old (lttng-analyses era).
  **Status: DONE — VALIDATED + BUILT OUT 2026-07-20; experiment support
  fixed and confirmed on the laptop 2026-07-22** (peam `01a201c`+`a1f5b40`:
  analyses un-struck on experiments, experiment runs working). All four
  analyses render as TC report tables (incl. user-created bar charts)
  against the 4-node trace; final architecture (peam commits `1da4c0b` →
  `a1f5b40`):
  - **Analysis scripts are TC-independent CLI tools** with a per-script
    *neutral table contract* (`TITLE`/`DESCRIPTION`/`TABLE_SPECS` with
    typed columns / `build_tables()`) driving three outputs: text (default),
    `--json`, `--csv`. Arguments: any mix of trace dirs / parent folders
    (everything beneath is included; run folder = whole multi-node run).
  - **All TC code quarantined in `peam/src/tc/`**: `lami.py` (protocol),
    `lami_adapter.py` (ONE generic neutral-contract→LAMI converter — no
    per-analysis LAMI code), `tc-common.sh` (PATH fixes, python3
    autodetect; config precedence tc-local.conf > env > defaults),
    `resolve_experiment.py` (experiment-name→member-folders, see below),
    committed two-line `lami_<name>.sh` entry points TC commands target,
    `tc-local.conf(.example)` for the three machine paths
    (PYTHON3/BABELTRACE2_DIR/TC_WORKSPACE), and `tc-setup.sh` — one-shot
    idempotent registration (TC closed) of the LAMI entries AND the
    `src/tc/*.xml` analyses directly into workspace metadata, `--remove`
    to uninstall (`b85859e`, `eb23662`).
  - Gotchas (empirical + TC 10.2 bytecode, 2026-07-22): `--mi-version`
    handshake mandatory or the entry is struck-through (verdict cached —
    restart TC); TC does no shell splitting (one-word command) and GUI
    PATH lacks homebrew; every result object needs `time-range`.
    **Real `--test-compatibility` semantics:** TC IGNORES the exit code
    and MERGES stderr into stdout — empty output or valid JSON without
    `error-message` = compatible, anything else (even a stray stderr
    warning) = struck out. So the compat path must be silent when
    compatible, emit `{"error-message": ...}` when not, and stay lenient
    on arguments it can't inspect; `lami.py` scopes compatibility to real
    PInsight traces (`is_pinsight_trace` scans CTF metadata).
    **Experiments:** TC passes the experiment NAME as the last argument
    (`TmfExperiment.getPath()` returns the name, not a path); membership
    is a shadow directory tree under `<project>/Experiments/<name>/`
    mirroring `<project>/Traces/` with 0-byte marker leaves — NOT the
    `.project` linked resources the first resolver parsed. Resolution now
    lives in standalone `resolve_experiment.py`.
    **macOS bash 3.2** cannot parse heredoc-inside-`$(…)`/`<(…)` (and has
    no mapfile) — this silently struck out every experiment from the Mac;
    any TC-facing shell path must avoid those constructs and be probed ON
    the Mac with `<wrapper> --test-compatibility <arg> 2>&1` (empty =
    compatible).
  - A new analysis = one Python file declaring the contract + a two-line
    .sh — text/json/csv/TC all inherited. The same contract is the intended
    input for WS5's report generator and WS7's Perfetto summaries.
- **EASE/TC-Scripting (if LAMI proves dead or too limited).** The official
  TC scripting integration can create real custom views (time graph, XY)
  from script data and bind scripts to menus; our analyses run as
  subprocesses feeding a small view-builder script. More power, no Java
  plugin, some install/API learning cost.
This bridge also softens WS3's scope: "select range → run Python analysis →
table in GUI" overlaps with what per-domain pattern XMLs would provide, using
logic we have already validated.

### WS10 — Flow graph: arrows for MPI messages, GPU launches, OMP fork/join *(added 2026-07-29)*

Goal: overlay causality on the timeline — MPI p2p send→recv between rank
rows, kernel launch→execution from rank row to device row, fork/join from a
master thread to its workers.

**Data readiness (assessed against current tracepoints):**
- **OMP fork/join — derivable today, no source change.** The key
  `(mpirank, parallel_codeptr, parallel_record_id)` is shared by
  `parallel_begin` (master, `omp_thread_num==0`) and every worker's
  `implicit_task_begin` (`ompt_callback.c` re-reads it from
  `parallel_data->ptr`); joins via `implicit_task_end` /
  `parallel_join_sync_*` → `parallel_end`; `team_size` on both ends.
  Caution: `omp_team_num` is declared on every event but never assigned
  (always 0) — never build hierarchy on it.
- **GPU launch→exec — feasible via the correlation id.**
  `correlationId` (CUPTI, u32) / `correlation_id` (roctracer, u64) is shared
  between host callback and activity record. Two caveats: activity records
  carry **no hostname/pid/mpirank** (match per trace dir, not payload), and
  their LTTng timestamp is buffer-flush time — the true interval is payload
  `start_gpu`/`end_gpu` (CUDA) / `begin_ns`/`end_ns` (HIP), which an XML
  state provider structurally cannot use (intervals key on the event's own
  timestamp; this is why activity records are excluded from the XML today).
  Clock offset comes from the one-shot `*_clock_calibration` events.
- **MPI messages — partially blocked by missing fields.** `source`/`dest`/
  `tag` are traced, so rank-pair arrows are drawable; but the
  **communicator is not traced** (matching is wrong for sub-communicator
  traffic — AMG's coarse levels), **bytes are not traced** (element `count`
  only; the datatype is dropped at the wrapper), and no request handle links
  Isend/Irecv to its Wait. Prerequisite source fix (small, localized in
  `pmpi_mpi.c`): call `PMPI_Type_size()` and emit `bytes`; emit a comm id
  (`PMPI_Comm_c2f` or a hash). Mind the ~10-field LTTng ceiling already hit
  twice in the CUPTI/roctracer headers.

**Platform: XML cannot draw arrows — verified.** `xmlView.xsd` in the
shipped `tmf.analysis.xml.core 4.3.2` allows a `timeGraphView` only
`head|definedValue|entry`; there is no link/arrow element. Arrows require a
Java `ITimeGraphDataProvider` implementing `fetchArrows()`. The building
blocks ship in the trace server jars: `tmf.core` event matching
(`ITmfEventMatching`; `TcpEventKey` is the reference pattern for a
send/recv key), `analysis.graph.core` (critical path), and trace
synchronization (`SyncAlgorithmFullyIncremental`) for cross-node clock skew.

**Plan — cheap validation before any Java:**
1. **Perfetto first (WS7, days):** `to_perfetto.py` emits the same pairings
   as flow arrows. Validates whether the view is readable at AMG scale
   (16 ranks × ~10⁵ p2p messages may render as noise) and which filters
   (selected rank, time window, min-bytes) it needs.
2. **Java plugin only if validated (2–4 weeks first time):** a headless
   bundle (`tmf.core` + `IDataProviderFactory` extension) dropped into
   `trace-compass-server/plugins/`, viewed via the VS Code trace extension —
   less code than an Eclipse RCP `tmf.ui` view, which can follow. No
   tycho/target-platform exists anywhere yet; standing up the TC dev
   environment dominates the estimate, not the matching logic. §7's
   `lbanalysis` plugin is a usable skeleton after renaming its bundle id and
   bumping deps from tmf 9.0 to the shipped 10.2. WS2's
   Host → Rank → {MPI, GPU, OMP} restructure is a readability prerequisite —
   in the current layout rank rows and device rows are not even adjacent.

### WS11 — Resource-efficiency / utilization XY views *(added 2026-07-29)*

Goal: XY charts of how efficiently hardware is used — CPU vs peak, memory
bandwidth, network bandwidth.

**Hard constraint: the trace contains none of this today.** No
PAPI/perf_event anywhere in the codebase; no uncore/HBM or NIC counters; the
only derivable bandwidth is explicit-GPU-copy GB/s (degenerate on MI300A
unified memory); energy is two events per process, no time series. Hence two
stages:

- **WS11a — time-in-state utilization now (extends WS4, zero source
  change).** XML `<xyView>` is supported by the shipped schema: the state
  provider writes numeric attributes, `tc-setup.sh` re-registers, no build
  step. Series: fraction of time in MPI-wait per rank, kernels-in-flight per
  device, MPI call rate, memcpy rate. `phase_detect.py`'s `collect()`
  already computes exactly these per 10 ms bin (`mpi_rate, mpi_frac,
  coll_share, kern_rate, act_frac, memcpy_rate, par_rate, omp_sync_frac`) —
  use that feature set as the metric seed so XML and Python stay
  dictionary-consistent (§5). This is
  "efficiency relative to the trace" (where time goes), not hardware
  efficiency.
- **WS11b — sampled hardware counters later (the third source change).**
  The long-planned periodic `energy_sample`-style event from the existing
  control thread, generalized to a counter sample. Backends in priority
  order: **amd-smi** (GPU activity %, VRAM, power — backend infrastructure
  already exists), **PAPI/perf_event** (CPU FLOPs, DRAM bandwidth),
  **Slingshot NIC counters via sysfs** (network). Emit scalar fields per
  device, not LTTng sequences (sequences are not addressable in TC XML state
  changes); node-wide values dedupe by hostname exactly as energy does. MPI
  byte volume as a network proxy additionally needs WS10's `bytes` fix.
  Rough effort: 1–2 weeks for the sample event + first backend; the TC side
  then reuses WS11a's xyView mechanics unchanged.

### WS12 — Source exploration: codeptr → file:line → open editor *(revived 2026-07-29)*

Goal (unchanged since the 2021 `SourceCodeLookup` note in
`pinsight-tracecompass`): from any event or region — in TC or in a report —
jump to the application source line that produced it.

**What was proven (2021, see §7):** stock TC "Open Source Code" works
end-to-end on a toy app (`lttng-tracecompass-srcwindow` demo: build with
`-g`, enable `lttng_ust_dl:*`, `add-context -t vpid -t ip` → Binary/
Function/Source Location columns → editor opens at the exact tracepoint
line). And it was proven **insufficient** for PInsight: the `ip` context
points inside the OMPT/PMPI/CUPTI callback, so on LULESH it opens
`ompt_callback.c`, not the application.

**The fix, designed then and still right:** resolve the **codeptr** fields
(`parallel_codeptr`, `mpi_codeptr`, `cuda_codeptr` — by definition the
return address into the *application*) instead of `ip`. Runtime prototypes
exist: the unmerged `origin/load_baseaddr` branch (2023; dladdr/`dli_fbase`
base-address subtraction so codeptrs are addr2line-able) and the cmake-gated
backtrace support (app-level IP discovery, `80ddc43`). Per
`callpath_profiling_design.md`, symbolization belongs **offline, never at
runtime** — and the runtime branch becomes unnecessary if
`lttng_ust_statedump:bin_info` (`baddr`, path, build-id) is captured:
offline `codeptr − baddr` → addr2line/DWARF.

Deliverables:
- **WS12a — Python symbolizer (days, anytime):** a `pinsight_reader` helper
  resolving codeptrs to `file:line (function)` given the binary (statedump
  `baddr` + addr2line). Every table/report that today prints hex lexgion ids
  gains readable names; feeds WS5 reports and WS7 Perfetto track names.
- **WS12b — TC integration (rides the WS10 Java decision):** replicate the
  debug-info "Open Source Code" action for codeptr fields — either the 2021
  PoC idea (an aspect substituting codeptr for `ip` so TC's own lookup
  machinery resolves it) or event-table aspects in the same plugin as WS10.
  Requires the binary reachable from the viewer machine (or a source-path
  mapping) — the srcwindow demo used a shared VM for exactly this reason.

## 4b. Implementation conventions (`analysis/` folder)

Layout rules (referenced from `analysis/README.md` §Extending):

- **Top level = app-agnostic, tool-agnostic** analysis scripts: they consume
  only PInsight's tracepoint schemas (pmpi / roctracer / energy / enter_exit
  domains), make no assumptions about which application produced the trace,
  and know nothing about any GUI/visualization tool.
- **`tc/` = all TraceCompass integration**; nothing outside `tc/` references
  TraceCompass. Contents per WS9 above (protocol, generic adapter, launcher
  logic, per-analysis wrappers, XMLs, setup script).
- **App-specific scripts** go in one subfolder per application (e.g. parsers
  of an app's own stdout); app-specific eval harnesses live in the separate
  pinsight-eval repo.

Neutral table contract (the one contract every frontend converts from): each
script declares `TITLE` / `DESCRIPTION` / `TABLE_SPECS` (typed columns) /
`build_tables()`, driving three outputs — text (default), `--json` (one
document: `{"analysis", "span_ns", "tables": [{name, title,
columns:[{name,type}], rows}]}`), `--csv` (one CSV per table, separated by
`# table: <name>` lines). Values are plain natural units (seconds, bytes,
0-1 ratios); column types: `int | string | number | duration_s | ratio |
bytes`. Tool adapters (TC/LAMI today; WS5 report generator and WS7 Perfetto
summaries later) convert from this contract, so a new analysis gets every
frontend for free by declaring it.

Conventions for new scripts:

- Build on `pinsight_reader.py` (runs `babeltrace2` over one or more trace
  dirs, merges them time-ordered, yields parsed events, provides begin/end
  duration matching; parallel decode by default, `PEAM_PAR=1` forces
  sequential) rather than re-parsing babeltrace2 text.
- Accept N trace paths; each may be an exact CTF dir or any parent folder
  (everything beneath is included); dedupe overlapping arguments.
- Multi-node runs: learn rank→host from the events themselves.
- Energy `gpu_uj` is node-wide (same values on every rank of a node) — dedupe
  by hostname, never sum across a node's ranks.

## 5. Metrics dictionary (seed — to grow into its own doc)
- **Categories (per rank):** `work` (app compute; not directly traced —
  wall minus accounted), `mpi_wait` (blocking MPI: Wait*, blocking
  Send/Recv, collectives), `mpi_post` (nonblocking posting), `gpu_sync`
  (hipStreamSync/hipDeviceSync/cuda sync), `gpu_launch`, `memcpy_host`
  (host time in copy calls), `omp_sync` (barriers/joins), `python_overhead`.
- **GPU device:** `kernel_exec` (activity), `copy_exec` (activity), `idle`.
- **Color rule:** waits = warm (reds/oranges), work/exec = cool
  (blues/greens), overheads = purple, idle = 10-15% alpha gray.
- Same names appear in XML `<definedValue>`s, Python dict keys, report
  legends.

## 6. Phasing & risks

**P1 (foundation):** WS1 manifest + WS2 restructure + WS6 hygiene.
Acceptance: 4-node experiment shows Host→Rank tree with readable labels,
physical devIds everywhere, idle states dimmed, no lost events at L=200.
**P2 (depth):** WS3 MPI + HIP + OMP refresh; WS4 XY views. Acceptance: MPI
latency distributions match mpi_latency.py numbers on the same trace.
**P3 (parity + reports):** WS3 CUDA/Python analyses; WS5 report generator.
Acceptance: single command yields an HTML report; CUDA/Python demo traces
render equivalently to HIP/MPI.
**P-web (parallel, decoupled from P1-P3):** WS7 Perfetto exporter (anytime —
depends only on the Python reader); WS8 trace-server pilot (after WS2, so the
pilot exercises the restructured XML).
**P4 (added 2026-07-29):** near-term additions — WS7 Perfetto (now doubling
as the WS10 arrow-validation gate), WS11a time-in-state xyViews, WS12a Python
symbolizer, and the WS10 MPI field fix (`bytes` + comm id; benefits every
downstream analysis, not just arrows). Gated/later: WS10 Java arrows (after
WS7 verdict + WS2), WS11b sampled counters (after WS1 — both touch the
control thread), WS12b TC source-lookup (rides the WS10 Java decision).
Frontend-strategy note: "XML-first"
means TC-**core**-first — the XML runs identically under Eclipse and the
trace server, so a later shift to a web/VS Code frontend forfeits none of
WS2-WS4. AI-assisted development makes the *glue* (converter, pilot,
reports) cheap; it does not make third-party frontend maturity problems
cheaper to own — hence hedges + evidence rather than an upfront platform bet.

**Progress snapshot (2026-07-24)**
- **DONE: WS9** (LAMI GUI↔Python bridge), including experiment support —
  validated on the laptop TC 2026-07-22. Side deliverables: neutral
  table contract + `--json`/`--csv` on all analyses, generic LAMI adapter,
  `tc-setup.sh` scripted registration/removal, peam restructure
  (`cfcbfa5`: `src/python`→`src`, XMLs→`src/tc`), README rewrites, legacy
  `old/` toolkit removed.
- **NOT STARTED: WS1–WS8.** Next per phasing is P1 (WS1 manifest + WS2
  restructure + WS6 hygiene); WS7 remains available anytime (depends only
  on `pinsight_reader.py`).
- Sequencing caution for WS1: it touches the same PInsight files as the
  in-flight uncommitted node-singleton measurement work
  (`pinsight_control_thread.c/.h`, `trace_config*`) — land or stash that
  first to avoid entangling the two changes.

**Progress snapshot (2026-07-29)**
- Toolkit relocated: `peam/src` → `pinsight/analysis` (three-repo split,
  2026-07-27); this doc now lives in `doc/design/`.
- New analyses since the last snapshot: `mpi_jitter.py` (committed,
  documented in `analysis/README.md`, but no `tc/lami_*.sh` wrapper) and
  `phase_detect.py` (change-point phases + iteration periodicity —
  committed 2026-07-29 with wrapper + README row, `f6e9c92`).
- trace-compass-server binary unpacked in the repo checkout (untracked);
  started 2026-07-07/08 but never fed a trace or our XML — WS8 remains
  not-started in substance.
- `pinsight-tracecompass` repurposed as the TC plugin repo (`2c48725`);
  its 2020-25 experiments inventoried in §7.
- WS10–WS12 added (this update). WS1–WS8 still not started.

**Risks / open questions**
- *(2026-07-29)* Arrow density: 16 ranks × ~10⁵ p2p messages may render as
  noise — hence the Perfetto-first gate on WS10; expect to need filtering
  (selected rank / time window / min-bytes).
- *(2026-07-29)* MPI matching correctness: without the communicator field,
  (src,dst,tag) matching is wrong for sub-communicator traffic; the WS10
  field fix must land before arrows are trusted on AMG.
- *(2026-07-29)* WS11b sampling overhead/perturbation from the control
  thread is unmeasured (that thread now also carries energy + node-singleton
  duties).
- *(2026-07-29)* Java plugin cost is front-loaded: no tycho/target-platform
  exists anywhere; the 2–4-week estimate is mostly TC dev-environment
  standup, not analysis logic.
- XML FSM correlation-id matching for launch→exec segments is unproven
  (fallback: that metric stays Python-only).
- One combined state provider at 16+ ranks × all domains: state-system size/
  build time may need pruning of unused attribute trees.
- All-domain parity needs CUDA and Python **test traces** (no current CUDA
  hardware in the loop; Python traces exist from the python_support work).
- Timeline entry trees keyed on manifest fields: XML entry `path` matching is
  static — hierarchy may need the state provider to *write* the tree in
  Host/Rank order rather than the view re-sorting it (design detail for WS2).

## 7. Prior experimental strands (`pinsight-tracecompass/experiment/`) — record + disposition *(added 2026-07-29)*

2020–2025 exploration, originally in this repo, moved out 2024-01-19
(`a377e1b`); the repo was repurposed for TC plugin development 2026-07-27
(`2c48725`). What exists, what it taught, and what carries forward:

- **3D trace visualization (2020–21, JavaFX-in-SWT).** Two artifacts:
  `JavaFX3DView/` (`Pinsight3DFXView`, TC 5.1/Java 8 plugin scaffold — the
  TMF event-request plumbing over `implicit_task_begin/end` is correct, but
  it still renders the stock random-box demo and the committed file has
  syntax errors), and the substantive one,
  `DebuggerIntegrationExperiment/pinsight3d.java` (764 lines): **thread ×
  parallel region (`parallel_codeptr`) × time**, one extruded box per
  implicit-task instance with depth = duration, data-sized axis walls,
  region-indexed colors. Loose file, no plugin manifest; box-click handlers
  exist but stop at `println` — the intended *click a box → open its source
  line* join was never made. That join is now WS12. The 2021 design note
  (`experiment/tracecompass/README.md`) preferred SWT/SWTChart+OpenGL for TC
  integration, yet everything built used JavaFX/FXCanvas — the
  integration-friction it predicted is real.
- **hwloc3d (2024, jzy3d/JOGL + Swing, standalone Maven — not a TC
  plugin).** Renders an lstopo XML export (JAXB from the hwloc DTD) as a 3D
  floorplan: Machine → Package → NUMA → cache → core nested boxes plus a PCI
  bridge tree. Works on the four committed machines; **topology-only, zero
  trace coupling**; per-machine hand-tuned scale constant; SWT binding
  explicitly parked ("SWT support is shaky" in the pom). The differentiated
  idea worth keeping: **paint trace-derived per-core/per-device metrics onto
  the hardware topology** — becomes interesting once WS11b provides sampled
  counters to paint.
- **Debugger integration (2021).** `debugging4performanc.md` vision:
  breakpoint-bounded tracing (`lttng rotate` at entry/exit breakpoints) with
  TC view + debugger + code editor in one Eclipse perspective.
  `DebuggerIntegrationExperiment/SampleView.java` demonstrated the enabling
  mechanism — rubber-band time selection on a chart broadcast via
  `TmfSelectionRangeUpdatedSignal`/`TmfWindowRangeUpdatedSignal` to all
  other views. Parked; the signal-broadcast pattern is reusable in any
  future RCP view.
- **Source-code lookup (2021 + 2023, feeds WS12).**
  `lttng-tracecompass-srcwindow/` proved stock TC "Open Source Code"
  end-to-end on a toy app (screenshots in-repo: `hello.c` opened at the
  tracepoint line from Binary/Function/Source Location columns).
  `SourceCodeLookup/README.md` recorded the PInsight-specific insight — the
  `ip` context resolves into PInsight's own callback code (LULESH screenshot
  opens `ompt_callback.c`), so app-level lookup must go through the codeptr
  fields — and proposed the codeptr-for-ip substitution PoC. The runtime
  half (base-address subtraction via dladdr/`dli_fbase`) sits on the
  unmerged `origin/load_baseaddr` branch (2023); the surviving merged pieces
  are the cmake-gated backtrace support and the backtrace-derived app-level
  `cuda_codeptr` (`80ddc43`). WS12 supersedes all of this with offline
  symbolization (statedump `baddr` + addr2line), per
  `callpath_profiling_design.md`.
- **lbanalysis (2025, SWTChart plugin).** Per-thread execution time bucketed
  by `parallel_codeptr`, with zoom and CSV export — a hand-rolled chart via
  `TmfEventRequest`, not a data provider. Usable as the WS10/WS12b plugin
  skeleton after renaming its bundle id (still
  `org.eclipse.tracecompass.tmf.sample.ui`) and bumping deps from tmf 9.0 to
  the shipped 10.2.

**Disposition:** the thread×region×time 3D content is now adequately served
by the 2D unified timeline + WS3 segments + WS7 Perfetto; 3D stays parked
unless the hwloc-metric-mapping idea is picked up post-WS11b. Any revival
should target the data-provider architecture (headless/TSP-friendly, so it
works under the trace server and web frontends), not FXCanvas-in-RCP. Source
exploration graduates from experiment to workstream as WS12; the debugger
perspective stays a long-horizon idea pending a concrete use case.
