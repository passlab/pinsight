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
| E4 | Castro | Multi-GPU | Introspection + AMR adaptivity demo | **Killer** |
| E5 | Castro Sedov | OpenMP 48T | OpenMP overhead on production code | Medium |

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

**Build**:
```bash
cd eva/Castro/Exec/hydro_tests/Sedov
# GNUmakefile: USE_CUDA=TRUE, USE_MPI=TRUE (1 rank/GPU), USE_OMP=FALSE
make DIM=3 -j48
```

**Run**: `mpirun -np 4 ./Castro3d.gnu.MPI.CUDA.ex inputs.3d.e2eval`

**Results**:
| Configuration | Avg Wall Time (s) | Median Time (s) | Overhead vs Baseline (s) | Overhead (%) |
|---------------|-------------------|-----------------|--------------------------|--------------|
| **Baseline**  | 32.32s            | 32.32s          | -                        | -            |
| **CUDA-only** | 35.11s            | 34.60s          | +2.28s                   | 7.0%         |
| **Dynamic50 (OFF)** | 35.66s      | 34.35s          | +2.03s                   | 6.3%         |
| **Dynamic50 (STANDBY)** | 37.81s  | 34.41s          | +2.09s                   | 6.4%         |
| **Dynamic50 (MONITORING)** | 40.64s| 39.98s         | +7.66s                   | 23.7%        |
| **Full Trace**| 41.58s            | 41.53s          | +9.21s                   | 28.5%        |

*Note: Trace volumes for ALL Dynamic50 subsets dropped from 48MB to ~1MB (98% reduction) with sub-10% overhead when properly dropping callbacks in OFF/STANDBY.*

**Key claim**: Rate-limited CUDA tracing has <10% overhead while successfully capturing top kernel hotspots precisely.

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

## E4: Castro Introspection + AMR Demo (Killer Experiment)

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

## E5: Castro OpenMP Overhead (48 threads, Sedov 3D)

**Goal**: Show PInsight works on Castro's OpenMP build.

**Build**: `USE_OMP=TRUE, USE_MPI=FALSE, USE_CUDA=FALSE`

**Same overhead comparison as E1 but on Castro's 100+ parallel regions.**

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
   5.4 In-Situ Introspection for AMR Applications (E4)
       - AMR performance evolution across introspection windows
       - Side-by-side TraceCompass screenshots
       - Analysis script demo
   5.5 Visualization (screenshots)
```

---

## Execution Order

1. Install Score-P and HPCToolkit on cci-aries
2. Build PInsight, LULESH, Castro (OpenMP + GPU)
3. **E1**: LULESH overhead runs (straightforward, gets infrastructure working)
4. **E2**: Castro multi-GPU overhead
5. **E3**: Castro GPU deep-dive traces + screenshots
6. **E4**: Castro introspection + AMR demo
7. **E5**: Castro OpenMP overhead (if time permits)
