# PInsight visualization & analysis redesign

**Status:** high-level design · 2026-07-17 · **progress update 2026-07-24**
(WS9 done incl. experiments; peam restructured — snapshot at end of §6)
**Decisions (fixed):** TraceCompass **XML-first** as the primary interactive
platform (no Java plugin, no trace-server for now) · PInsight source changes
limited to **one new manifest event** (everything else post-processing; no
energy_sample yet) · **all domains** (OpenMP, MPI, HIP, CUDA, Python, energy)
brought to the same level, not just the AMG/MI300A path.

## 1. Current assets (three disconnected layers)

| Layer | What exists | State |
|---|---|---|
| Trace data | 7 tracepoint domains; PMPI carries `count/dest/source/tag`; HIP host callbacks **and** GPU activity records (`correlation_id`, GPU-side `begin/end_ns`); energy enter/exit brackets (node-wide counters); multi-node = per-node CTF dirs | Richer than any view exploits |
| TraceCompass XML (`peam/src/tc/`) | `pinsight_analysis.xml`: ONE combined state provider + time-graph view (per the 2026-06-29 decision: cross-domain correlation on a shared time axis requires a single provider/view). Location trees: Processor, OS/OMP Thread, OMP Team/Region, MPI Rank, HIP Device/Kernel, CUDA Device/Kernel. `pinsight_omp_pattern_analysis.xml`: FSM segment analysis, **OpenMP-only, stale** | Working but shallow; activity records excluded |
| Python (`peam/src/`) | `pinsight_reader.py` + load_imbalance / mpi_latency / gpu_datamovement / halo_exchange / mpi_gpu_energy_report / parse_energy | Validated on the 4-node run; since 2026-07-20 each script declares the neutral table contract (text/`--json`/`--csv`) and runs from the TC GUI (WS9) |

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
pilot exercises the restructured XML). Frontend-strategy note: "XML-first"
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

**Risks / open questions**
- XML FSM correlation-id matching for launch→exec segments is unproven
  (fallback: that metric stays Python-only).
- One combined state provider at 16+ ranks × all domains: state-system size/
  build time may need pruning of unused attribute trees.
- All-domain parity needs CUDA and Python **test traces** (no current CUDA
  hardware in the loop; Python traces exist from the python_support work).
- Timeline entry trees keyed on manifest fields: XML entry `path` matching is
  static — hierarchy may need the state provider to *write* the tree in
  Host/Rank order rather than the view re-sorting it (design detail for WS2).
