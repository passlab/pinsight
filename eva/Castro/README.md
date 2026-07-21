# Castro Evaluation for PInsight

This directory contains everything needed to reproduce the PInsight overhead
evaluation using [Castro](https://github.com/AMReX-Astro/Castro), an
adaptive-mesh compressible hydrodynamics code for astrophysical flows.

## Directory Layout

```
eva/Castro/
├── setup_castro.sh        # Clone Castro + apply patches + copy inputs
├── scripts/               # Benchmark launch scripts and trace configs
│   ├── e2_bench.sh            # E2: baseline vs full tracing
│   ├── e2_bench_full.sh       # E2: all tracing modes (off/rate-limited/cuda-only)
│   ├── e2_bench_optimized.sh  # E2: all modes including baseline
│   ├── e2_cuda_only.install   # Trace config: CUDA tracing only
│   ├── e2_off.install         # Trace config: all tracing OFF
│   └── e2_rate_limited.install # Trace config: rate-limited (50 traces/lexgion)
├── inputs/                # Custom Castro input files for evaluation
│   ├── inputs.3d.e2eval       # Sedov 3D, 4×GPU, 128³ base + 2 AMR, 200 steps
│   └── inputs.3d.e5eval       # Sedov 3D, CPU-only (OpenMP+MPI), 128³ + 1 AMR, 30 steps
├── patches/               # Local patches applied on top of upstream Castro
│   └── 0001-fix-std-format.patch  # std::format → iostream (compiler compat)
├── results/               # Benchmark results, one subfolder per machine
│   └── cci-aries/             # Results from cci-aries (4×A100)
└── source/                # Castro source checkout (GITIGNORED, not committed)
```

## Quick Start

### 1. Set up Castro source

```bash
./setup_castro.sh
```

This clones Castro at the pinned commit (`e5cc3f72f`), initializes AMReX and
Microphysics submodules, applies local patches, and copies evaluation input
files into the Sedov build directory.

### 2. Build the Sedov problem

```bash
cd source/Exec/hydro_tests/Sedov
make -j$(nproc)
```

This produces the `Castro3d.gnu.MPI.CUDA.ex` executable (or the CPU-only
variant depending on `GNUmakefile` settings).

### 3. Run benchmarks

```bash
# Full evaluation (baseline + all PInsight tracing modes)
./scripts/e2_bench_optimized.sh

# Quick baseline vs full-tracing comparison
./scripts/e2_bench.sh
```

### 4. Save results

Copy or redirect output to the results directory for your machine:

```bash
mkdir -p results/$(hostname)
./scripts/e2_bench_optimized.sh > results/$(hostname)/e2_optimized_results.txt 2>&1
```

## Porting to a New Machine

1. Clone the pinsight repo
2. Run `eva/Castro/setup_castro.sh`
3. Adjust `GNUmakefile` if needed (e.g., `COMP`, `USE_CUDA`, `USE_OMP`)
4. Build and run benchmarks
5. Save results under `results/<machine-name>/`
