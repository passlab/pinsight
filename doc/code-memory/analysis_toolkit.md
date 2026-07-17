---
name: analysis-toolkit
description: "peam/src/python structured analysis toolkit (2026-07-17) — layout conventions, the 4+2 scripts, and first findings on the 4-node AMG run"
metadata: 
  node_type: memory
  type: project
  originSessionId: d4091f5d-393c-4694-91ed-c2780d3c93e6
---

Structured PInsight trace-analysis toolkit lives in `peam/src/python/` (started 2026-07-17,
consolidating scripts previously scattered under eva/AMG2023-tuolumne/). See its README.md.
Related: [[amg2023-eval-overhead]].

**Why:** user wants all future analysis scripts built here in a structured way — app-agnostic
scripts at top level, app-specific under `<app>/` subfolders (e.g. `amg2023/`), `old/` is a
legacy early-PInsight toolkit to ignore/not build on.

**How to apply:** new analysis scripts go here, built on `pinsight_reader.py` (shared
babeltrace2 reader: takes N trace dirs — pass all per-node dirs of a multi-node run,
babeltrace2 merges time-ordered; yields Event objects + BeginEndMatcher for begin/end
durations). Don't re-parse babeltrace2 text ad hoc.

Scripts (all tested against the fixed 4-node run 20260717_124305_4n):
- `load_imbalance.py`, `mpi_latency.py`, `gpu_datamovement.py`, `halo_exchange.py` — the four
  targeted analyses (per-rank wait/work; call-duration distributions same-vs-cross-node and
  by size; H2D/D2H/D2D copies host+device side; P2P-vs-collective + neighbor topology +
  size profile).
- `mpi_gpu_energy_report.py` — generalization/rename of the old analyze_amg.py (deleted; its
  never-used dev2rank bug fixed: per-rank GPU attribution now only when unambiguous
  one-GPU-per-rank, else degrades with a note). Reads babeltrace2 on stdin (unlike the rest).
- `parse_energy.py` (moved from calib/, app-agnostic), `amg2023/parse_overhead.py` (moved,
  AMG-stdout-specific). `analyze_per_rank.py` left behind in eva/ (superseded).

**PMPI tracepoint fields are richer than the old scripts used** (verified in real trace):
P2P begin events carry `count` (MPI elements, datatype NOT traced), `dest`/`source`, `tag`;
memcpy host events carry `hipMemcpyKind` enum + byte count; activity records carry bytes +
GPU-side begin/end_ns but NOT direction. Cross-rank send→recv matching for true one-way
latency is possible field-wise but node clock skew makes it unreliable — scripts stick to
host in-call durations.

**First findings on the 4-node/16-rank L=200 run** (worth follow-up):
- Systematic imbalance: the LAST local rank on each node (3/7/11/15) MPI-waits 3-4× more
  (~9-11s) than the first (~2-3s); z-position in the 2x2x4 grid correlates. Rank 0 waits
  least (straggler others wait for).
- MPI_Waitall dominates MPI cost (55.6s all-rank total); MPI_Allreduce heavy tail (p50
  0.4ms, p99 69ms, max 885ms — arrival-time skew, consistent with the imbalance).
- Halo:collective time = 2.4:1. Message-size profile: 63% of halo messages ≤64 elements but
  0.2% of volume → coarse-grid LATENCY-bound tail (multigrid classic).
- GPU copies: ~75GB/device activity at only ~7 GB/s avg; blocking hipMemcpy D2H host time
  huge (77s all-rank for only 4GB) → mostly implicit synchronization (waiting for kernels),
  not transfer. D2D volume ~500GB (unified-memory MI300A).
- Neighbor topology sane: 17 neighbors/rank (27-pt stencil), z-neighbors same-node,
  x/y cross-node, ~2800 msgs per heavy pair.
