# PInsight trace-analysis scripts (Python)

Structured analysis toolkit for PInsight CTF traces. Started 2026-07-17; the
`old/` subfolder is a legacy toolkit from an early PInsight version — kept for
reference, not maintained, do not build on it.

## Layout

- top level — **app-agnostic, tool-agnostic** analysis scripts: they consume
  only PInsight's tracepoint schemas (pmpi / roctracer / energy / enter_exit
  domains), make no assumptions about which application produced the trace,
  and know nothing about any GUI/visualization tool.
- `tc/` — **all TraceCompass integration**: the LAMI protocol (`lami.py`),
  ONE generic adapter (`lami_adapter.py` — converts any analysis's neutral
  table contract to LAMI mechanically; no per-analysis LAMI code), the
  shared launcher logic (`tc-common.sh`: PATH/interpreter setup +
  experiment-name resolution), and one ready-to-use two-line wrapper per
  analysis (`lami_<name>.sh` — the files TC entries point at). Machine
  specifics go in an optional gitignored `tc/tc-local.conf`. Nothing
  outside `tc/` references TraceCompass.
- `amg2023/` — AMG2023-specific scripts (e.g. parsers of AMG's own stdout).
  Add one subfolder per application for anything app-specific.

## Shared infrastructure

- `pinsight_reader.py` — common reader: runs `babeltrace2` over one or more
  trace directories (pass all of a multi-node run's per-node dirs; they are
  merged time-ordered), yields parsed events, and provides begin/end duration
  matching. All analysis scripts below build on it. Requires `babeltrace2` on
  `PATH`.

## Analysis scripts (app-agnostic)

All take one or more paths: `python3 <script>.py [--json|--csv] <path> [...]`.
Each path may be an exact CTF trace dir (contains `metadata`) **or any parent
folder — every trace found beneath it is included**. So: pass a run folder for
the whole multi-node run, a node folder for one node, or cherry-pick several
folders from different runs. Overlapping arguments are deduplicated; all
selected traces are time-merged.

Output formats: default = human-readable text; `--json` = one JSON document
(`{"analysis", "span_ns", "tables": [{name, title, columns:[{name,type}],
rows}]}`); `--csv` = CSV per table (multi-table analyses separate tables with
`# table: <name>` comment lines). Values are plain natural units (seconds,
bytes, 0-1 ratios); column types: int | string | number | duration_s | ratio
| bytes. Each script declares this contract as `TITLE`/`DESCRIPTION`/
`TABLE_SPECS`/`build_tables()` — tool adapters (e.g. TraceCompass, future
report generators) convert from this ONE contract, so new analyses get every
frontend for free by declaring it.

- `load_imbalance.py` — per-rank wall/wait/work breakdown across all ranks of
  a run; identifies which ranks wait (and the likely straggler they wait for).
- `mpi_latency.py` — MPI call-duration distributions (mean/p50/p95/p99/max)
  per call type, split same-node vs cross-node and by message size. Note:
  durations are host time in-call (skew-free), not cross-rank one-way latency.
- `gpu_datamovement.py` — host<->device copy analysis: direction/bytes/host
  time from hipMemcpy host events; actual GPU copy time and bandwidth from
  activity records.
- `halo_exchange.py` — point-to-point (halo) vs collective cost split,
  neighbor topology (who talks to whom, same-node vs cross-node), and the
  message-size profile (latency-bound vs bandwidth-bound diagnosis).
- `mpi_gpu_energy_report.py` — the combined per-rank report (MPI/GPU/energy +
  figures + kernel hotspots); reads a `babeltrace2` stream on stdin:
  `babeltrace2 <trace> | python3 mpi_gpu_energy_report.py --tag run1`.
  Per-rank GPU-exec attribution requires an unambiguous one-GPU-per-rank
  mapping and degrades gracefully (with a note) otherwise.
- `parse_energy.py` — minimal per-device energy/power from the energy domain
  only: `python3 parse_energy.py <trace_dir> [...]` (one line per dir).

## App-specific

- `amg2023/parse_overhead.py` — trace-vs-notrace overhead comparison from
  AMG2023's own stdout timing lines (not from trace data); tied to AMG's
  output format by nature.

## TraceCompass GUI integration (LAMI "External Analyses")

The four analysis scripts above are runnable from the TraceCompass GUI
(validated against TC 2026-06 on 2026-07-20): right-click a trace → External
Analyses → the analysis → results render as native report tables (charts
available from the report view). Shared protocol code: `lami.py`
(`--mi-version` / `--metadata` / `--test-compatibility` handshakes,
`--begin/--end` time-range filtering, mandatory `time-range` on results).

Design rule: the analysis scripts are plain TC-independent CLI tools; ALL
TraceCompass-specific code lives under `tc/` — `lami.py` (protocol),
`lami_adapter.py` (one GENERIC adapter converting any analysis's neutral
table contract to LAMI), `tc-common.sh` (shared launcher logic), and the
per-analysis entry points `lami_<name>.sh`.

Setup on the analysis machine (needs python3 + babeltrace2 + this whole
folder including `tc/`):
0. **One-shot automated setup:** with TraceCompass CLOSED, run
   `tc/tc-setup.sh` — it registers all four external analyses AND installs
   the `src/tc/*.xml` data-driven analyses directly into the
   workspace metadata (locations confirmed against TC source: LAMI
   `.properties` under `...analysis.lami.core/user-defined-configs/`, XML
   files under `...tmf.analysis.xml.core/xml_files/`). Idempotent — re-run
   after `git pull` or a folder move. Manual per-entry steps below are the
   fallback/reference.
1. Point each TC external-analysis command at the committed wrapper's
   absolute path, e.g. `<here>/tc/lami_load_imbalance.sh` — no per-machine
   generation or editing. Wrappers are needed because TC treats the command
   as ONE executable (no shell splitting) and GUI apps get a minimal PATH;
   `tc-common.sh` handles that (probes /opt/homebrew/bin, /usr/local/bin,
   ~/homebrew/bin; workspace defaults to ~/eclipse-workspace). Three paths
   are customizable — `PYTHON3`, `BABELTRACE2_DIR`, `TC_WORKSPACE` — either
   in the marked CONFIG block at the top of `tc-common.sh`, or (preferred:
   survives `git pull`, keeps machine paths out of the repo) in gitignored
   `tc/tc-local.conf`, which overrides the CONFIG block. Start from the
   documented template: `cp tc/tc-local.conf.example tc/tc-local.conf`.
2. Add via right-click External Analyses → add; the entry validates via the
   `--mi-version` handshake on selection (struck-through = handshake failed;
   TC caches the verdict — restart TC after fixing anything).
3. Select a time range in a timeline view first to run the analysis on just
   that window (TC passes `--begin/--end`).
4. Run on a single trace, or type more trace/run folders into the run
   dialog's extra-arguments field (folders expand, overlaps dedup).
5. Running on an EXPERIMENT works via the wrapper: TC passes the
   experiment's NAME (always the last argument), and the wrapper resolves
   it to the member trace folders by parsing the workspace tracing
   project's `.project` linked resources — before the Python script ever
   sees it. The Python side itself never learns about TC experiments.

## Conventions for new scripts

- App-agnostic scripts at top level, app-specific ones under `<app>/`.
- Build on `pinsight_reader.py` rather than re-parsing babeltrace2 text.
- Multi-node runs: accept N trace dirs, learn rank->host from the events.
- Energy gpu_uj is node-wide (same values on every rank of a node) — dedupe
  by hostname, never sum across a node's ranks.
