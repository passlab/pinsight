# SC26 Evaluation Plan: LULESH + Castro on cci-aries (48-Core + 4×A100)

All evaluation data collected fresh on the cci-aries system.

## System: cci-aries

- **CPU**: 2× AMD EPYC 7413 24-Core Processor (48 physical cores, 96 threads with SMT)
  - Base clock: 2.65 GHz, Boost: 3.6 GHz
  - L1d: 1.5 MiB (48 instances), L1i: 1.5 MiB (48 instances)
  - L2: 24 MiB (48 instances), L3: 256 MiB (8 instances)
  - 8 NUMA nodes, 6 cores per NUMA node per socket
- **GPU**: 4× NVIDIA A100-PCIE-40GB (Compute Capability 8.0)
  - Driver: 580.126.20
- **Memory**: 503 GB DDR4
- **OS**: Ubuntu 22.04.5 LTS, Kernel 6.8.0-106-generic
- **Compiler**: Clang/LLVM 21.1.8 with LLVM OpenMP runtime (OMPT 5.0 support)
- **LTTng**: 2.13.4 (Nordicité)
- **Comparison tools**: Score-P (latest), HPCToolkit (latest, spack install)
- **Benchmarks**: LULESH (OpenMP proxy-app), Castro (production AMR, OpenMP + multi-GPU)

---

## Step 0: Tool Installation

### Score-P
```bash
# Download from https://www.score-p.org/
wget https://perftools.pages.jsc.fz-juelich.de/cicd/scorep/tags/scorep-8.4/scorep-8.4.tar.gz
tar xzf scorep-8.4.tar.gz && cd scorep-8.4
./configure --prefix=$HOME/tools/scorep --with-nocuda --with-nocupti
make -j48 && make install
export PATH=$HOME/tools/scorep/bin:$PATH
```

### HPCToolkit
```bash
# Spack-based install (recommended)
git clone https://github.com/spack/spack.git ~/spack
source ~/spack/share/spack/setup-env.sh
spack install hpctoolkit
spack load hpctoolkit
```

### PInsight
```bash
cd /home/yyan7/tools/pinsight
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j48
# libpinsight.so in build/
```

---

## Experiments Overview

| Exp | Benchmark | Parallelism | Goal | Priority |
|-----|-----------|-------------|------|----------|
| E1 | LULESH | OpenMP 48T | Overhead & trace size vs Score-P, HPCToolkit | High |
| E2 | Castro Sedov | **Multi-GPU (4×A100)** | Overhead & trace reduction on production AMR | **Highest** |
| E3 | Castro Sedov | Multi-GPU | GPU tracing with rate control + domain filtering | **Highest** |
| E4 | LULESH | OpenMP 24T | Closed-loop introspection + adaptive thread-count tuning | **Killer** |
| E5 | Castro Sedov | OpenMP 48T | OpenMP overhead on production code | Medium |
| E6 | Castro | Multi-GPU | Introspection + AMR adaptivity demo | Future |

> Castro is the lead benchmark. LULESH provides familiar baseline comparison.

---

## E1: LULESH Overhead Comparison (OpenMP, 48 threads)

**Goal**: Quantify overhead & trace reduction vs Score-P/HPCToolkit.

**Configs**:
| Config | Detail |
|--------|--------|
| Baseline | No tool |
| PInsight Dynamic (20 traces/region) | Rate-limited |
| PInsight Dynamic (200 traces/region) | Medium sampling |
| PInsight Selective (4 regions) | Only hot regions |
| PInsight Full Trace | All events, all invocations |
| Score-P Full Tracing | Default config |
| HPCToolkit (300 samples/s) | Default sampling rate |

**Metrics**: wall time (avg 10 runs), trace size, overhead %. 
**Deliverable**: Comparison table (updated Table 1).

---

## E2: Castro Multi-GPU Overhead (4×A100, Sedov 3D)

**Goal**: Show PInsight CUPTI tracing overhead is acceptable on multi-GPU production code.

**Status**: ✅ **Complete** (re-evaluated 2026-04-07 with 4-mode architecture) — See [EvaluationPlan-E2.md](file:///home/yyan7/tools/pinsight/doc/EvaluationPlan-E2.md) for full details.

**Build**:
```bash
cd eva/Castro/Exec/hydro_tests/Sedov
# GNUmakefile: USE_CUDA=TRUE, USE_MPI=TRUE (1 rank/GPU), USE_OMP=FALSE
make DIM=3 -j48
```

**Run**: `mpirun -np 4 ./Castro3d.gnu.MPI.CUDA.ex inputs.3d.e2eval` (5 runs per config)

**Results**:
| Configuration | Avg Wall Time (s) | Median Time (s) | Overhead (%) | Trace Size |
|---|---|---|---|---|
| **Baseline** | 32.04 | 31.98 | — | — |
| **Dynamic50 (MONITORING)** | 33.84 | 33.93 | **5.6%** | ~988K |
| **Dynamic50 (OFF)** | 34.04 | 34.11 | **6.2%** | ~988K |
| **Dynamic50 (STANDBY)** | 34.22 | 34.15 | **6.8%** | ~988K |
| **CUDA-only** | 34.02 | 33.89 | **6.2%** | ~892K |
| **Full Trace** | 34.66 | 34.58 | **8.2%** | 48M |

*Note: All Dynamic50 modes produce ~1MB traces (98% reduction vs full 48MB). All modes under 9% overhead after 4-mode architecture redesign.*

**Key claim**: All PInsight tracing modes have **<9% overhead** on multi-GPU production code. Rate-limited tracing achieves **98% trace volume reduction** while capturing kernel hotspots precisely.

---

## E3: Castro GPU Tracing Deep Dive

**Goal**: Demonstrate fine-grained GPU trace analysis capabilities.

**Config**:
```ini
[CUDA.global]
    trace_mode = TRACING
    CUDA.device = (0-3)
[MPI.global]
    trace_mode = MONITORING
[Lexgion.default]
    max_num_traces = 100
    tracing_rate = 10
    trace_mode_after = MONITORING
```

**Deliverables**:
- Per-kernel timing breakdown from trace analysis
- TraceCompass screenshot showing GPU kernel timeline across 4 devices
- Identification of dominant compute kernels (hydro reconstruct, Riemann solver)
- Trace volume comparison: full vs rate-limited

---

## E4: LULESH Closed-Loop Introspection + Adaptive Thread Tuning

**Goal**: Demonstrate PInsight's closed-loop introspection capability: automatically trace a window of parallel region executions, pause the application, launch an analysis script in-situ, update per-region thread-count knobs, and resume — all without restarting.

**Status**: ✅ **Complete** — See [E4_walkthrough.md](file:///home/yyan7/tools/pinsight/doc/E4_walkthrough.md) for full details.

**Setup**: LULESH 32³, 24 threads (numactl nodes 0–3), 1000 iterations, 4 runs per config.

**Instrumentation**: 30 OpenMP parallel regions instrumented with `pinsight_get_knob_int()` for dynamic per-region thread control:
```c
#pragma omp parallel for num_threads(pinsight_get_knob_int("integrate_stress_elem"))
    for (Index_t k=0; k<numElem; ++k) { ... }
```

**Config** (`e4_introspect_uniform.cfg`):
```ini
[OpenMP]
    trace_mode = TRACING
    trace_mode_after = INTROSPECT:60:/path/to/analyze_and_tune.sh:STANDBY

[Lexgion.default]
    trace_starts_at = 0
    max_num_traces = 50
    tracing_rate = 1

[Knob]
    integrate_stress_elem = 24
    kinematics            = 24
    velocity              = 24
    # ... (all 30 regions initialized to 24)
```

**Analysis Script** (`analyze_and_tune.sh`): Classifies 30 regions into Heavy/Medium/Light based on source code analysis, sets thread counts (H=MAX_T, M=MAX_T×2/3, L=MAX_T/3), updates config via `sed`, sends `SIGUSR1` to reload.

**Results**:

| Configuration | Avg FOM (zones/s) | vs Baseline |
|---|---|---|
| Baseline TBB (no PInsight knobs) | 8,415 | — |
| Uniform-24 (all knobs=24, STANDBY mode) | 8,426 | +0.1% |
| Static-tuned H=24/M=16/L=8 | 8,330 | −1.0% |
| Auto-tuned (INTROSPECT → script) | 8,127 | −3.5% |

**Overhead Decomposition**:

| Source | Total (ms) | % |
|---|---|---|
| `num_threads()` clause — Uniform vs. Baseline | −5.1 | −0.13% (negligible) |
| Thread-team resizing — Static vs. Uniform | +44.7 | +1.15% |
| INTROSPECT pause — Auto vs. Static | +98.2 | +2.50% |

**Key Findings**:
- Per-transition resize cost: **2.24 µs**
- Barrier savings net-positive only at **≥ 48T** (all 8 NUMA nodes)
- `num_threads()` clause overhead: **0.13%** (negligible)
- INTROSPECT pause duration: **< 1 second** (98 ms median)
- Critical `fork()` → `posix_spawn()` bug fix for multithreaded safety

**Paper deliverables**: Equations 1–4 and Tables III–IV in eva.tex §V-C (break-even model, FOM results, scaling table).

---

## E5: Castro OpenMP Overhead (48 threads, Sedov 3D)

**Goal**: Show PInsight works on Castro's OpenMP build.

**Build**: `USE_OMP=TRUE, USE_MPI=FALSE, USE_CUDA=FALSE`

**Same overhead comparison as E1 but on Castro's 100+ parallel regions.**

---

## E6: Castro Introspection + AMR Demo (Future Experiment)

**Goal**: Show introspection's unique value for AMR — as mesh evolves, performance shifts. Only continuous introspection captures this.

**Scenario**:
1. Castro Sedov 3D with AMR on 4×A100
2. Configure cyclic introspection:
```ini
[Lexgion.default]
    max_num_traces = 200
    tracing_rate = 1
    trace_mode_after = INTROSPECT:60:analyze_castro.sh:TRACING
```
3. Each introspection window captures a different AMR phase:
   - **Window 1** (initial): Uniform mesh, all kernels balanced
   - **Window 2** (shock forms): AMR refines around shock front, workload shifts
   - **Window 3** (propagation): Further refinement, GPU load imbalance emerges
4. Analysis script (`analyze_castro.sh`) uses babeltrace2 to:
   - Compute per-kernel avg duration across GPUs
   - Detect load imbalance across A100s
   - Output per-window performance summary

**Why AMR + introspection is the killer demo**:
- Static tracing captures only ONE mesh state
- Post-mortem analysis misses the evolution over regrid cycles  
- PInsight's introspection captures the **changing** performance landscape
- No other tool can do this without restarting the application

**Paper deliverables**:
- 3-window performance timeline showing shifting hotspots
- TraceCompass screenshots from 2 different introspection windows (side-by-side)
- Analysis script output demonstrating automated hotspot detection
- Narrative: "the dominant kernel shifts from X to Y as AMR refines"

---

## TraceCompass Screenshots Plan

| Figure | Content | Purpose |
|--------|---------|---------|
| Fig A | Castro GPU kernel timeline (4×A100) | Show multi-device tracing capability |
| Fig B | Two introspection windows side-by-side | Show AMR performance evolution |
| Fig C (optional) | LULESH CPU/thread view | Familiar OpenMP trace visualization |

---

## Paper Evaluation Section Structure

```
5. Evaluation
   5.1 Experimental Setup
       - System specs (48-core AMD EPYC, 4×A100)
       - Benchmarks (LULESH, Castro)
       - Comparison tools (Score-P, HPCToolkit)
   5.2 Measurement Overhead and Trace Volume (E1 + E2)
       - LULESH comparison table
       - Castro multi-GPU comparison table
   5.3 GPU Trace Analysis (E3)
       - Per-kernel breakdown
       - TraceCompass GPU timeline screenshot
   5.4 Closed-Loop Introspection and Adaptive Tuning (E4)
       - LULESH per-region thread-count tuning
       - FOM results and overhead decomposition
       - Break-even analysis across NUMA topologies
   5.5 Visualization (screenshots)
```

---

## Execution Order

1. ~~Install Score-P and HPCToolkit on cci-aries~~ ✅
2. ~~Build PInsight, LULESH, Castro (OpenMP + GPU)~~ ✅
3. ~~**E1**: LULESH overhead runs~~ ✅
4. ~~**E4**: LULESH introspection + adaptive thread tuning~~ ✅
5. ~~**E2**: Castro multi-GPU overhead~~ ✅
6. **E3**: Castro GPU deep-dive traces + screenshots
7. **E5**: Castro OpenMP overhead (if time permits)
8. **E6**: Castro introspection + AMR demo (future)
