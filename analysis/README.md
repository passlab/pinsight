# Analyzing PInsight Traces — User Guide

This folder contains the tools for analyzing PInsight CTF traces, usable two
ways:

1. **Command line** — Python analysis scripts that print human-readable
   reports (or JSON/CSV) from one or more trace folders.
2. **TraceCompass GUI** — the same analyses run from the TraceCompass
   right-click menu, plus XML-defined timeline views, via the integration in
   [`tc/`](tc/).

For the design and implementation of this toolkit (architecture, the neutral
table contract, TraceCompass/LAMI internals, roadmap), see
[`doc/design/visualization_analysis_redesign.md`](../doc/design/visualization_analysis_redesign.md).

## Prerequisites

- `python3`
- [`babeltrace2`](https://babeltrace.org) on `PATH` (used by the common trace
  reader)

No installation step is needed — run the scripts directly from this folder.

## Quick start

```bash
# Whole multi-node run: pass the run folder, per-node traces merge automatically
python3 analysis/load_imbalance.py ./traces/amg-4node-run1

# One node only
python3 analysis/load_imbalance.py ./traces/amg-4node-run1/node0

# Machine-readable output
python3 analysis/mpi_latency.py --json ./traces/amg-4node-run1
python3 analysis/mpi_latency.py --csv  ./traces/amg-4node-run1
```

### Trace path arguments

Every script takes one or more paths. Each path may be:

- an exact CTF trace directory (the one containing `metadata`), or
- **any parent folder** — every trace found beneath it is included.

So you can pass a run folder for the whole multi-node run, a node folder for
one node, or cherry-pick several folders from different runs. Overlapping
arguments are deduplicated, and all selected traces are merged time-ordered.

### Output formats

- default — human-readable text report
- `--json` — one JSON document: `{"analysis", "span_ns", "tables": [...]}`
- `--csv` — CSV per table (multi-table analyses separate tables with
  `# table: <name>` comment lines)

Values are in plain natural units: seconds, bytes, 0–1 ratios.

## Analysis scripts

All are **app-agnostic**: they consume only PInsight's tracepoint schemas and
make no assumptions about which application produced the trace.

| Script | What it tells you |
|--------|-------------------|
| `load_imbalance.py` | Per-rank wall/wait/work breakdown across all ranks; which ranks wait, and the likely straggler they wait for |
| `mpi_latency.py` | MPI call-duration distributions (mean/p50/p95/p99/max) per call type, split same-node vs cross-node and by message size |
| `mpi_jitter.py` | Blocking-wait jitter per MPI call type: cross-rank spread (straggler signal) and p99/p50 tail ratio (jitter signal); compare against a baseline run with `--baseline DIR` for the amplified-jitter verdict |
| `gpu_datamovement.py` | Host↔device copy analysis: direction/bytes/host time from `hipMemcpy` host events; actual GPU copy time and bandwidth from activity records |
| `halo_exchange.py` | Point-to-point (halo) vs collective cost split, neighbor topology (who talks to whom, same-node vs cross-node), and message-size profile (latency-bound vs bandwidth-bound) |
| `mpi_gpu_energy_report.py` | Combined per-rank report (MPI/GPU/energy + figures + kernel hotspots) |
| `parse_energy.py` | Minimal per-device energy/power summary (one line per trace dir) |

Notes:

- `mpi_latency.py` durations are host time in-call (skew-free), not cross-rank
  one-way latency.
- `mpi_jitter.py` extra options: `--calls A,B,..` restricts the analyzed MPI
  calls (default auto-detects, ranked by total wait); `--top N` sets how many
  auto-detected calls to report (default 6).
- `mpi_gpu_energy_report.py` reads a `babeltrace2` stream on stdin instead of
  taking paths:
  `babeltrace2 <trace> | python3 mpi_gpu_energy_report.py --tag run1`.
  Per-rank GPU-exec attribution requires an unambiguous one-GPU-per-rank
  mapping and degrades gracefully (with a note) otherwise.
- `pinsight_reader.py` is not an analysis — it is the shared trace reader all
  scripts build on.

## TraceCompass GUI

The analyses in this folder are runnable from the TraceCompass GUI:
right-click a trace or experiment → **External Analyses** → pick the analysis
→ results render as native report tables (charts can be created from the
report view). The [`tc/`](tc/) folder also ships XML data-driven analyses
(unified PInsight timeline and OpenMP pattern analysis).

### Setup (once per analysis machine)

The machine running TraceCompass needs `python3`, `babeltrace2`, and this
whole folder (including `tc/`).

1. *(Optional but recommended)* Record your machine paths in a local config
   that survives `git pull`:
   ```bash
   cp tc/tc-local.conf.example tc/tc-local.conf
   # then edit PYTHON3, BABELTRACE2_DIR, TC_WORKSPACE as needed
   ```
2. With TraceCompass **closed**, run the one-shot installer:
   ```bash
   bash tc/tc-setup.sh
   ```
   This registers the external analyses **and** installs the `tc/*.xml`
   analyses into the workspace metadata. It is idempotent — re-run it after a
   `git pull` or after moving this folder. `tc/tc-setup.sh --remove`
   uninstalls.
3. Start TraceCompass. The entries appear under **External Analyses** in the
   project tree.

If you prefer manual registration: right-click **External Analyses** → add,
and point the command at the absolute path of the committed wrapper, e.g.
`<this folder>/tc/lami_load_imbalance.sh` (TraceCompass needs the one-word
wrapper; do not point it at the Python scripts directly).

### Running analyses from the GUI

- **Single trace:** right-click the trace → External Analyses → the analysis.
- **Time range:** select a time range in a timeline view first — the analysis
  then runs on just that window.
- **Experiments:** right-click the experiment works the same way; the wrapper
  resolves the experiment to its member traces automatically.
- **Extra traces:** type additional trace/run folders into the run dialog's
  extra-arguments field (folders expand, overlaps dedup).

### Troubleshooting

- **Entry shown struck-through** — the compatibility handshake failed
  (missing `python3`/`babeltrace2`, wrong paths in `tc/tc-local.conf`, or the
  selected trace is not a PInsight trace). TraceCompass caches the verdict:
  **restart TraceCompass after fixing anything.**
- To probe a wrapper outside the GUI:
  `tc/lami_load_imbalance.sh --test-compatibility <trace>` — empty output
  means compatible.

## Extending

A new analysis is one Python file declaring the neutral table contract plus a
two-line `tc/lami_<name>.sh` wrapper — text/JSON/CSV output and the
TraceCompass integration are all inherited. See the *Implementation
conventions* section of
[`doc/design/visualization_analysis_redesign.md`](../doc/design/visualization_analysis_redesign.md)
for the contract, layout rules, and conventions.
