# LULESH Evaluation for PInsight

This directory contains everything needed to reproduce the PInsight evaluation
using [LULESH](https://github.com/LLNL/LULESH) (Livermore Unstructured
Lagrangian Explicit Shock Hydrodynamics), an OpenMP/MPI shock-hydro proxy app.
It is the primary workload for the **E1 tracing-overhead** and
**E4 in-situ introspection** experiments, plus the application-knob, energy,
and mode-switch feature demonstrations.

## Directory Layout

```
eva/LULESH/
├── setup_lulesh.sh        # Clone LULESH at pinned commit + apply PInsight patch
├── patches/               # Local patches applied on top of upstream LULESH
│   └── 0001-pinsight-lulesh.patch  # Makefile build flags + knob num_threads() instrumentation
├── scripts/               # Benchmark launch scripts and trace configs
│   ├── run_e1_full_eval.sh     # E1: overhead sweep (baseline/OFF/STANDBY/MONITORING/TRACING/rate), Score-P & TBB
│   ├── run_e1_eval.sh          # E1: single-size baseline vs PInsight modes vs Score-P
│   ├── run_e1_smoke.sh         # E1: quick smoke test
│   ├── run_e4_eval.sh          # E4: baseline vs uniform vs static-tuned vs auto-tuned (INTROSPECT)
│   ├── run_cyclic_introspect_test.sh  # Cyclic INTROSPECT loop
│   ├── run_knob_bench.sh / _l2.sh     # Application-knob per-region num_threads sweep
│   ├── run_energy_bench.sh     # RAPL energy comparison
│   ├── run_lulesh_bench.sh / _lttng.sh # Mode/rate overhead micro-benchmarks
│   ├── run_pause_test.sh       # INTROSPECT pause/resume demo
│   ├── run_tbb_eval.sh         # TBB-malloc + HPCToolkit comparison
│   ├── test_bidir_mode_switch.sh      # Bidirectional SIGUSR1 mode switching
│   ├── analyze_and_tune.sh / cyclic_analyze.sh  # INTROSPECT analysis scripts (spawned by PInsight)
│   ├── *.install               # Domain trace configs (OpenMP / MPI / CUDA)
│   ├── e4_*.cfg, cyclic_introspect.cfg          # INTROSPECT / cyclic configs
│   ├── pinsight_*.txt          # Knob + full trace configs
│   └── trace_config_*.txt      # Rate / SIGUSR / pause-test configs
├── results/               # Benchmark results, one subfolder per machine
│   └── cci-aries/             # Results from cci-aries (dual-socket CPU, up to 60³, 8–48 threads)
│       ├── results_30/ 32/ 48/ 60/   # E1 overhead sweeps by problem size (e1_results.csv + e1_eval.log)
│       ├── results_e4/               # E4 introspection runs + E4_report.md
│       ├── results_cyclic/           # Cyclic INTROSPECT logs
│       ├── bench_results_s30*.txt    # Mode/rate micro-benchmark output
│       ├── energy_results_*.txt      # Energy sweep output
│       ├── knob_bench_*.txt          # Application-knob sweep output
│       └── trace_guided_results_*.txt
└── source/                # LULESH source checkout + build (GITIGNORED, not committed)
```

Note: raw CTF trace directories produced by the runs are **not** committed — only
the timing logs and analysis summaries are kept under `results/`.

## Quick Start

### 1. Set up LULESH source

```bash
./setup_lulesh.sh
```

This clones LULESH at the pinned clean-upstream commit (`3e01c40`) and applies
`patches/0001-pinsight-lulesh.patch`, which:

* switches the Makefile to the PInsight build flags (adds `-I$(PINSIGHT_ROOT)/src`,
  builds & links `app_knob.o`), and
* instruments `lulesh.cc` with `num_threads(pinsight_get_knob_int("<region>"))`
  on the hot OpenMP loops, enabling the application-knob experiments.

The Makefile defaults to `clang++-21`; edit `SERCXX`/`MPICXX` in
`source/Makefile` for your machine's compilers if needed.

### 2. Build LULESH

```bash
cd source
make -j$(nproc)          # produces lulesh2.0
```

### 3. Run benchmarks

LULESH takes its workload on the command line (no input files):
`-s <per-domain cube size>` and `-i <iterations>`, with `OMP_NUM_THREADS`
selecting the thread count.

```bash
# E1 overhead sweep (problem size, thread counts, repeat count are args)
./scripts/run_e1_full_eval.sh 60 "8 16 24 32 48" 5

# E4 in-situ introspection (auto-tuned vs static vs baseline)
./scripts/run_e4_eval.sh

# Application-knob per-region num_threads sweep
./scripts/run_knob_bench.sh
```

Scripts resolve all paths relative to their own location, find the PInsight
library at `<repo>/build/libpinsight.so`, and run the binary from `source/`.
Results are written under `results/$(hostname -s)/`.

External-tool paths (TBB malloc proxy, HPCToolkit, Score-P) are overridable via
environment variables (`TBB_MALLOC`, `HPCTOOLKIT_LIBDIR`, `HPCRUN`, `PATH`) with
the cci-aries defaults as fallbacks.

## Porting to a New Machine

1. Clone the pinsight repo and build `libpinsight.so`.
2. Run `eva/LULESH/setup_lulesh.sh`.
3. Adjust `source/Makefile` compilers if needed, then `make` in `source/`.
4. Run scripts in `scripts/`; save output under `results/<machine-name>/`.

## Notes

* The INTROSPECT configs (`e4_*.cfg`, `cyclic_introspect.cfg`) embed an
  **absolute path** to their analysis script (`scripts/analyze_and_tune.sh`,
  `scripts/cyclic_analyze.sh`) because PInsight `posix_spawn`s it by path.
  Update that path for your checkout location.
* These configs use the pre-rename `trace_mode_after` key (the era the
  cci-aries results were collected). Current PInsight uses `window_end_action`
  — regenerate or update the configs before re-running against a newer build.
