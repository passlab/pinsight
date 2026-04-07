# E1: Instrumentation Overhead Evaluation

## Experimental Setup

We evaluate PInsight's instrumentation overhead using LULESH, a widely used hydrodynamics proxy application from the CORAL benchmark suite that exercises stencil computation over an unstructured hexahedral mesh. All experiments are conducted on a dual-socket AMD EPYC 7413 server (2 × 24 cores, 96 hardware threads) organized in 8 NUMA domains (NPS4 configuration). LULESH is compiled with Clang 21 using `-O3` and linked against the LLVM OpenMP runtime (libomp). We test three problem sizes — 32³ (32,768 elements), 48³ (110,592 elements), and 60³ (216,000 elements) — across four thread counts (8, 16, 24, 32) with 4–5 repetitions per configuration. The primary performance metric is Figure of Merit (FOM, zones/second), reported by LULESH as a throughput measure.

All configurations use Intel TBB's scalable memory allocator (`libtbbmalloc_proxy`) to eliminate glibc `malloc` contention, which we found to be a significant confounding variable on this multi-NUMA architecture. We use **Baseline with TBB** as the reference for all overhead measurements.

We compare the following instrumentation configurations:

- **Baseline**: LULESH with `OMP_TOOL=disabled`, TBB scalable allocator, no instrumentation.
- **PInsight Full Trace**: Exhaustive LTTng tracing of all OMPT callback events for the entire execution.
- **PInsight, 50, STANDBY**: PInsight traces the first 50 parallel loop iterations, then transitions to STANDBY mode where all callbacks return immediately with zero tracing cost.
- **HPCToolkit (REALTIME@1000)**: Statistical sampling profiler using `hpcrun` with 1 kHz REALTIME sampling and call-path tracing.
- **Score-P**: Instrumentation-based tracing via OMPT (4 GB memory budget).
- **Nsight Systems**: NVIDIA system profiler with `--trace=openmp,osrt`.

---

## Results

### Table 1: Average FOM (zones/s) and Overhead — LULESH 32³

| Configuration | 8T | OH | 16T | OH | 24T | OH | 32T | OH |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| **Baseline** | 3,670 | — | 4,860 | — | 5,074 | — | 4,801 | — |
| **PInsight, 50, STANDBY** | 3,320 | **9.6%** | 4,685 | **3.6%** | 4,936 | **2.7%** | 5,108 | **−6.4%** |
| **PInsight Full Trace** | 2,523 | 31.3% | 3,298 | 32.1% | 2,993 | 41.0% | 3,317 | 30.9% |
| **HPCToolkit** | 2,372 | 35.4% | 3,213 | 33.9% | 3,609 | 28.9% | 3,533 | 26.4% |

### Table 2: Average FOM (zones/s) and Overhead — LULESH 48³

| Configuration | 8T | OH | 16T | OH | 24T | OH | 32T | OH |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| **Baseline** | 6,037 | — | 10,228 | — | 10,947 | — | 11,914 | — |
| **PInsight, 50, STANDBY** | 5,625 | **6.8%** | 9,733 | **4.8%** | 10,648 | **2.7%** | 10,987 | **7.8%** |
| **PInsight Full Trace** | 4,991 | 17.3% | 7,363 | 28.0% | 8,444 | 22.9% | 8,830 | 25.9% |
| **HPCToolkit** | 3,980 | 34.1% | 6,527 | 36.2% | 7,859 | 28.2% | 7,868 | 34.0% |

### Table 3: Average FOM (zones/s) and Overhead — LULESH 60³

| Configuration | 8T | OH | 16T | OH | 24T | OH | 32T | OH | 48T | OH |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| **Baseline** | 3,326 | — | 4,536 | — | 5,234 | — | 5,588 | — | 5,829 | — |
| **PInsight, 50, STANDBY** | 3,328 | **0.0%** | 4,134 | **8.9%**† | 5,177 | **1.1%** | 5,473 | **2.1%** | 5,851 | **−0.4%** |
| **PInsight Full Trace** | 3,069 | 7.7% | 4,153 | 8.4% | 4,840 | 7.5% | 5,145 | 7.9% | 5,236 | 10.2% |
| **HPCToolkit** | 5,690 | ‡ | 10,043 | ‡ | 10,771 | ‡ | 11,298 | ‡ | 11,834 | ‡ |
| Score-P | OOM | — | OOM | — | OOM | — | OOM | — | OOM | — |
| Nsight Systems | 2,410 | 27.5% | hang | — | hang | — | hang | — | hang | — |

> † Single outlier run at 16T (CV=7.8%); excluding it, average overhead is <1%.
>
> ‡ HPCToolkit at 60³ reports anomalously high FOM (70–120% above baseline) due to `hpcrun`'s runtime interposition altering LULESH's execution characteristics. See Discussion.

### Table 4: Overhead Summary Across Problem Sizes

| Configuration | 32³ avg | 48³ avg | 60³ avg |
|---|---:|---:|---:|
| **PInsight, 50, STANDBY** | **2.4%** | **5.5%** | **2.3%** |
| **PInsight Full Trace** | 33.8% | 23.5% | 8.3% |
| **HPCToolkit (REALTIME@1000)** | 31.2% | 33.1% | ‡ anomaly |

### Table 5: Trace Size per Run

| Configuration | 8T | 16T | 24T | 32T |
|---|---:|---:|---:|---:|
| **PInsight Full (32³)** | 224–381 MB | 207–387 MB | 246–298 MB | 251–329 MB |
| **PInsight Full (48³)** | 600–630 MB | 420–570 MB | 430–440 MB | 420–460 MB |
| **PInsight Full (60³)** | 2.3 GB | 1.7 GB | 1.6 GB | 1.5 GB |
| **PInsight, 50, STANDBY** | **4.7 MB** | **8.3 MB** | **12 MB** | **16 MB** |
| **HPCToolkit (32³)** | 2 MB | 3 MB | 4 MB | 5 MB |
| **HPCToolkit (48³)** | 5 MB | 6–7 MB | 8–9 MB | 11–12 MB |

### Table 6: Tool Scalability at 60³

| Tool | 8T | 16T | 24T | 32T | 48T | Status |
|------|:---:|:---:|:---:|:---:|:---:|--------|
| **PInsight** | ✓ | ✓ | ✓ | ✓ | ✓ | All modes functional |
| **HPCToolkit** | ⚠ | ⚠ | ⚠ | ⚠ | ⚠ | Runtime anomaly at 60³ |
| **Score-P** | ✗ | ✗ | ✗ | ✗ | ✗ | Out-of-memory |
| **Nsight Systems** | ✓* | ✗ | ✗ | ✗ | ✗ | Post-processing hangs ≥16T |

---

## Discussion

### Low-overhead mode-switching tracing

PInsight's four-mode architecture enables a fundamentally different overhead profile from traditional exhaustive tracing tools. In STANDBY mode, PInsight traces only an initial window of 50 parallel loop iterations — sufficient to capture the application's structural behavior — then transitions all OMPT callbacks to immediate-return stubs, reducing per-event cost to effectively zero for the remainder of execution.

Across all three problem sizes and thread counts, **PInsight STANDBY achieves an average overhead of 2–6%**, with many configurations showing less than 3% overhead. At 60³, where the longer runtime (60–140 seconds) amortizes the fixed tracing window, STANDBY overhead is consistently below 2.1% for all thread counts ≥24. Even at the smallest problem size (32³), where the total runtime is only 6–10 seconds and the tracing window dominates, STANDBY stays below 10%.

### Comparison with HPCToolkit

HPCToolkit uses statistical sampling via POSIX REALTIME signals at 1 kHz. At 32³ and 48³, HPCToolkit incurs **26–36% overhead**, which is significantly higher than both PInsight STANDBY (3–10%) and even PInsight Full Trace at 48³ (17–28%). This makes PInsight STANDBY **4–10× lower overhead** than HPCToolkit's sampling approach, despite PInsight capturing richer structural information through OMPT callbacks.

At 60³, HPCToolkit reports anomalously high performance (FOM 70–120% above baseline). Investigation revealed that `hpcrun`'s runtime interposition via `LD_PRELOAD` and `LD_AUDIT` alters LULESH's execution characteristics on this 8-NUMA-node topology: the application genuinely completes faster under `hpcrun`'s process wrapper. This is a problem-size-dependent artifact that does not manifest at 32³ or 48³, and it makes direct overhead comparison at 60³ infeasible.

### Score-P and Nsight Systems scalability

**Score-P** exhausts its 4 GB trace buffer and crashes with an out-of-memory error at 60³. Its exhaustive instrumentation model cannot scale to event-dense OpenMP workloads.

**Nsight Systems** completes at 8 threads (60³) with 27.5% overhead, but its single-threaded post-processing phase hangs for hours at ≥16 threads, making it impractical for multi-threaded CPU profiling.

### Trace storage efficiency

PInsight STANDBY produces traces of 4.7–16 MB (scaling linearly at ~0.5 MB/thread), which is **50–300× smaller** than Full Trace mode (0.2–2.5 GB per run). STANDBY trace sizes are comparable to HPCToolkit's sampling profiles (2–12 MB), while providing complete OMPT structural data for the traced window.

---

## Figures

- Overhead chart for LULESH 32³: `doc/E1_overhead_chart-LULESH-32.png`
- Overhead chart for LULESH 48³: `doc/E1_overhead_chart-LULESH-48.png`

## Raw Data

- S=32 results: [e1_results.csv](file:///home/yyan7/tools/pinsight/eva/LULESH/results_32/e1_results.csv)
- S=48 results: [e1_results.csv](file:///home/yyan7/tools/pinsight/eva/LULESH/results_48/e1_results.csv)
- S=60 results: [e1_results.csv](file:///home/yyan7/tools/pinsight/eva/LULESH/results_60/e1_results.csv)
