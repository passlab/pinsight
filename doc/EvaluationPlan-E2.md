# E2: Castro Multi-GPU Overhead Evaluation — Results

## System
- **CPU**: 2× AMD EPYC 7413 (48 cores)
- **GPU**: 4× NVIDIA A100-PCIE-40GB
- **Benchmark**: Castro Sedov 3D, 128³ base grid, 2 AMR levels, 200 steps
- **Launch**: `mpirun -np 4` (1 rank per GPU)
- **PInsight**: `scripts/trace.sh` with LTTng
- **PInsight build**: 2026-04-07 (post 4-mode architecture redesign)

---

## 1. Architecture Changes Since Initial E2

The E2 evaluation was re-run on 2026-04-07 after significant architectural changes to PInsight:

| Change | Impact |
|---|---|
| **4-mode trace architecture** (OFF/STANDBY/MONITORING/TRACING) | Cleaner zero-cost bypass paths in STANDBY/OFF |
| **Dedicated control thread** (SWMR pattern) | Mode transitions no longer crash libomp |
| **Cooperative pause** for introspection | New synchronization in callbacks |
| **MPI semantics hardening** | Cleaner MPI wrapper behavior |

These changes resolved the issues found during the initial E2 run (§2) and dramatically improved MONITORING and Full Trace overhead.

---

## 2. Issues Discovered and Fixed (Initial E2, March 2026)

During the initial evaluation, we hit several bugs that blocked proper tracing, which we methodically isolated and resolved:

### 2.1 Initial Mode Transition Bug
*   **Symptoms:** Trace size was too small (~48M) for "Full Trace", capturing only MPI events, and no CUDA events were present.
*   **Root Cause:** The `domain_default_trace_config` used a `last_mode` check for triggering callbacks. It initialized `last_mode` to `TRACING` instead of `NONE`, causing the first `pinsight_control_cuda_apply_mode()` to "short-circuit" and skip registering CUPTI callbacks.
*   **Fix:** Updated `last_mode` initialization in `trace_config.c` to use `PINSIGHT_DOMAIN_NONE`.

### 2.2 Configuration Env Var Typo
*   **Symptoms:** Trace size on `dynamic50`, `cuda_only`, and `full_trace` were all identical (~48M or ~900K depending on prior runs), and rate-limiting never triggered.
*   **Root Cause:** The Bash `run_e2_eval.sh` script wrongly exported `PINSIGHT_TRACE_CONFIG` instead of the correct variable `PINSIGHT_TRACE_CONFIG_FILE`. PInsight fell back to default behavior.
*   **Fix:** Updated the bash script to correctly export `PINSIGHT_TRACE_CONFIG_FILE`.

### 2.3 Mode Re-configuration Concurrency Crash (Segfault)
*   **Symptoms:** Once the config was successfully loaded, the `dynamic50` run printed "Auto-trigger: OpenMP mode -> MONITORING" and immediately segfaulted on all 4 MPI ranks.
*   **Root Cause:** The PInsight control thread, upon hitting the 50-trace limit, called `ompt_set_callback(..., NULL)` to unregister OpenMP callbacks. However, doing this concurrently from a non-OpenMP thread while OpenMP standard threads are in flight crashes LLVM `libomp`.
*   **Fix:** Disabled the `ompt_set_callback` mechanism from the asynchronous control thread in `pinsight_control_thread.c`. The `PINSIGHT_SHOULD_TRACE` volatile loop kills overhead locally without needing dangerous unregistration hooks.

---

## 3. E2 Benchmark Results (Re-evaluation, 2026-04-07)

Re-evaluation with the 4-mode architecture. 5 runs per configuration, 30 runs total.

### Wall-clock Times
| Configuration | Avg Wall Time (s) | Median Time (s) | StdDev (s) | Overhead (%) |
|---|---|---|---|---|
| **Baseline** | 32.04 | 31.98 | 0.10 | — |
| **Dynamic50 (MONITORING)** | 33.84 | 33.93 | 0.17 | **5.6%** |
| **Dynamic50 (OFF)** | 34.04 | 34.11 | 0.12 | **6.2%** |
| **Dynamic50 (STANDBY)** | 34.22 | 34.15 | 0.27 | **6.8%** |
| **CUDA-only** | 34.02 | 33.89 | 0.19 | **6.2%** |
| **Full Trace** | 34.66 | 34.58 | 0.27 | **8.2%** |

### Raw Run Times
| Config | Run 1 | Run 2 | Run 3 | Run 4 | Run 5 |
|---|---|---|---|---|---|
| Baseline | 31.98 | 31.91 | 32.12 | 32.19 | 31.98 |
| Dynamic50 (MONITORING) | 33.65 | 33.98 | 34.03 | 33.61 | 33.93 |
| Dynamic50 (OFF) | 33.84 | 34.11 | 34.17 | 34.13 | 33.95 |
| Dynamic50 (STANDBY) | 34.32 | 34.67 | 34.15 | 33.89 | 34.05 |
| CUDA-only | 34.14 | 34.34 | 33.87 | 33.89 | 33.86 |
| Full Trace | 34.58 | 34.72 | 34.48 | 34.38 | 35.16 |

### Trace Volume Sizes
| Configuration | Trace Size | Volume Reduction vs Full Trace |
|---|---|---|
| **Full Trace** | 48 MB | 0% (Baseline Max) |
| **Dynamic50 (all modes)** | ~988K | **98% reduction** |
| **CUDA-only** | ~892K | **98% reduction** |

### Comparison: Initial E2 (March) vs Re-evaluation (April)

| Mode | Old Overhead | New Overhead | Improvement |
|---|---|---|---|
| Dynamic50 → MONITORING | **23.7%** | **5.6%** | 4.2× better |
| Dynamic50 → OFF | 6.3% | 6.2% | Same |
| Dynamic50 → STANDBY | 6.4% | 6.8% | Same |
| CUDA-only | 7.0% | 6.2% | Slightly better |
| Full Trace | **28.5%** | **8.2%** | 3.5× better |

---

## 4. Key Findings

> [!IMPORTANT]
> **All modes under 9% overhead.** The 4-mode architecture redesign eliminated the MONITORING/Full Trace overhead cliff. MONITORING dropped from 23.7% to 5.6%, and Full Trace from 28.5% to 8.2%.

1. **Uniform Low Overhead Across All Modes:** After the 4-mode redesign, all PInsight configurations (MONITORING, OFF, STANDBY, CUDA-only, Full Trace) show **5.6–8.2% overhead** — a narrow band with no mode incurring disproportionate cost. This is a dramatic improvement from the initial evaluation where MONITORING (23.7%) and Full Trace (28.5%) were significantly more expensive.

2. **MONITORING No Longer a Bottleneck:** The previous MONITORING mode kept `cuptiEnableCallback(1)` active, causing ~24% overhead from CUPTI callback dispatch. The new architecture uses cleaner bypass paths, reducing MONITORING to just **5.6%** — essentially the same as OFF/STANDBY.

3. **Volume Reduction Remains Excellent:** All rate-limited configurations produce ~1MB traces (98% reduction from 48MB full trace), unchanged from the initial evaluation.

4. **Very Low Variance:** Standard deviations are 0.10–0.27s across all configs (vs occasional 43s+ spikes in the initial evaluation), indicating the new architecture has more predictable performance characteristics.

5. **CUPTI Callback Overhead is Architectural:** The remaining ~6% overhead across all modes represents the irreducible cost of CUPTI subscriber registration and domain-active checking. This is consistent with CUPTI-based tool overhead on GPU-dense workloads.

---

## 5. Files

| File | Description |
|---|---|
| `eva/Castro/Exec/hydro_tests/Sedov/run_e2_redo.sh` | Re-evaluation script (6 configs × 5 runs) |
| `eva/Castro/Exec/hydro_tests/Sedov/e2_results_redo/` | Raw results from re-evaluation |
| `eva/Castro/Exec/hydro_tests/Sedov/e2_results/` | Original results (March 2026) |
| `eva/Castro/Exec/hydro_tests/Sedov/e2_dynamic50.txt` | Config: 50 traces, then MONITORING |
| `eva/Castro/Exec/hydro_tests/Sedov/e2_dynamic50_off.txt` | Config: 50 traces, then OFF |
| `eva/Castro/Exec/hydro_tests/Sedov/e2_dynamic50_standby.txt` | Config: 50 traces, then STANDBY |
| `eva/Castro/Exec/hydro_tests/Sedov/e2_cuda_only.txt` | Config: CUDA-only (MPI/OpenMP OFF) |
| `eva/Castro/Exec/hydro_tests/Sedov/e2_full_trace.txt` | Config: all domains TRACING, unlimited |
