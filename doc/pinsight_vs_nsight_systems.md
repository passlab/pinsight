# PInsight vs. NVIDIA Nsight Systems: CUDA Support Comparison

> **Purpose**: Reference for SC26 paper related work section and overhead comparison planning.
> Both tools use CUPTI internally; the fundamental differences are in tool model, trace format,
> analysis paradigm, and overhead control philosophy — not in the CUPTI APIs themselves.

---

## 1. CUPTI API Usage — Same Core, Different Scope

Both tools use the same two CUPTI mechanisms:
- **Callback API** for CPU-side synchronous interception of CUDA API calls
- **Activity API** for GPU-side asynchronous execution timestamps

The differences are in which events are captured, at what depth, and what is done with them.

| CUPTI Feature | Nsight Systems | PInsight |
|---|---|---|
| Callback API (Runtime) | All 200+ API calls | Selected 9 (HPC-relevant) |
| Callback API (Driver level) | ✅ Yes | ❌ No |
| `ACTIVITY_KIND_MEMCPY` | ✅ | ✅ |
| `ACTIVITY_KIND_KERNEL` | ✅ | ✅ |
| `ACTIVITY_KIND_DRIVER` | ✅ | ❌ |
| `ACTIVITY_KIND_CUDA_EVENT` | ✅ | ❌ |
| `ACTIVITY_KIND_UNIFIED_MEMORY` | ✅ | ❌ |
| `ACTIVITY_KIND_OPENACC/GL/VK` | ✅ | ❌ |
| CUPTI Profiling API (SM util, warp efficiency, L2 hit rate, etc.) | ✅ via `libnvperf_host` | ❌ |
| CUDA Graphs — node-level tracing | ✅ Hardware Event System | ❌ |
| NVTX user annotations | ✅ | ❌ (uses lexgions instead) |
| Clock calibration | Continuous hardware-level sync | One-shot `cuda_clock_calibration` event |
| Sole subscriber constraint | ✅ (blocks other CUPTI tools) | ✅ (same constraint) |

---

## 2. Fundamental Architectural Difference: Tool Model

This is the most important distinction for the paper.

| Dimension | Nsight Systems | PInsight |
|---|---|---|
| **Deployment** | External profiler: `nsys profile ./app` | `LD_PRELOAD` — no recompile, no launcher |
| **Analysis model** | **Post-hoc only** — collect all, analyze offline | **In-situ capable** — introspection while app runs |
| **App feedback** | None — static trace | Active — script rewrites config, adjusts knobs |
| **Runtime control** | Start/stop, fixed trigger intervals | SIGUSR1, per-site auto-trigger, introspection cycle |
| **Scope** | System-wide: CPU, GPU, OS, NVLink, power, PCIe | HPC programming model events (OpenMP, MPI, CUDA) |
| **Trace format** | Proprietary `.nsys-rep` (SQLite internally) | Open **CTF** (Common Trace Format) |
| **Analysis tools** | Nsight UI only | babeltrace2, Trace Compass, custom Python scripts |

---

## 3. Multi-Programming-Model Support

| Dimension | Nsight Systems | PInsight |
|---|---|---|
| **OpenMP** | Partial (CPU sampling + limited OMPT) | Full OMPT (35+ events), correlated with CUDA |
| **MPI** | Separate per-rank traces, merged in GUI | Unified via PMPI; `mpirank` field in every CUDA event |
| **CUDA** | Primary focus, very deep | ✅ HPC-relevant subset |
| **Cross-domain lexgion** | ❌ Independent timelines per domain | ✅ OpenMP→CUDA nesting on shared LIFO stack |
| **Unified trace** | ❌ Separate lanes, correlated by timestamp | ✅ Single CTF stream with thread context per event |

When an OpenMP thread launches a CUDA kernel, PInsight nests the CUDA lexgion inside the active
OpenMP parallel region on the same thread-local stack.  This cross-domain nesting is impossible
to represent in Nsight's model, where CUDA and OpenMP are independent tool domains.

---

## 4. Overhead and Rate Control

| Dimension | Nsight Systems | PInsight |
|---|---|---|
| **Typical overhead** | 5–30% (full activity capture) | <5% rate-limited; <1% MONITORING mode |
| **Rate control** | Per activity kind (binary on/off) | Per-call-site (lexgion): `max_num_traces`, `tracing_rate` |
| **Selective tracing** | Domain-level enable/disable | Per-event, per-site; hot vs. cold path |
| **Long-running apps** | Trace grows unboundedly | Bounded by `max_num_traces`; auto-transitions to MONITORING |

Nsight Systems overhead can spike on memory-bandwidth-bound codes because every DMA and kernel
record is captured, proprietary compression is applied, and there is no user-level filtering.

---

## 5. What Only Nsight Has (Not in PInsight by Design)

- **Hardware GPU metrics**: SM utilization, warp efficiency, L2 cache hit rate, memory bandwidth
  utilization — require CUPTI Profiling API (`libnvperf_host`), incompatible with Activity API
- **CUDA Graphs**: node-level execution tracing via the Hardware Event System
- **System-wide events**: NVLink bandwidth, PCIe traffic, OS scheduling, GPU power/thermal
- **Driver-level API tracing**: lower than CUDA Runtime

These are intentionally out of scope for PInsight, which targets programming-model-level analysis
rather than microarchitectural optimization.

---

## 6. What Only PInsight Has (Not in Nsight Systems)

- **In-situ introspection loop**: pause, analyze, reconfigure, resume — all without restarting
- **Per-call-site rate-limited tracing**: each unique `codeptr` gets independent trace budget
- **Auto-trigger**: when a lexgion reaches `max_num_traces`, automatically switch mode or introspect
- **Application performance knobs**: per-region thread count tuning via the same config infra
- **Unified OpenMP + MPI + CUDA trace** in a single CTF stream
- **Open trace format**: CTF, analyzable without vendor tool lock-in

---

## 7. Notes for Related Work Section

Suggested framing (2–4 sentences for the paper):

> NVIDIA Nsight Systems~\cite{NsightSystems} is the most capable GPU profiler available,
> using the same CUPTI Callback and Activity APIs as PInsight to instrument CUDA applications.
> Its strengths — full hardware metric collection, CUDA Graph node tracing, and system-wide
> visibility — come at the cost of 5–30\% overhead, a proprietary trace format requiring
> vendor tools for analysis, and a fundamentally post-hoc model that precludes in-situ
> interaction.  PInsight targets the complementary design point: open trace format, unified
> multi-model tracing (OpenMP, MPI, CUDA) in a single stream, and rate-limited in-situ
> introspection that enables feedback-driven adaptation during a running simulation.

Cite as: NVIDIA. Nsight Systems User Guide, 2024. https://docs.nvidia.com/nsight-systems/

---

## 8. Overhead Comparison Plan (E2/Castro)

### What to measure

For a fair comparison on Castro Sedov 3D (4×A100, 4 MPI ranks, USE_CUDA=TRUE):

| Config | Tool | Settings |
|---|---|---|
| Baseline | None | No LD_PRELOAD |
| PInsight Full | PInsight | All CUDA events, no rate limit |
| PInsight Rate-limited | PInsight | `max_num_traces=50`, `tracing_rate=1` |
| PInsight MONITORING | PInsight | CUDA domain MONITORING mode |
| **Nsight Full** | **nsys** | **Default full profile** |
| **Nsight GPU-only** | **nsys** | **CUDA activity only, no CPU sampling** |

### Nsight run command templates

```bash
# Full profile (equivalent to PInsight full trace)
nsys profile --trace=cuda,nvtx,osrt \
             --output=/tmp/castro_nsight_full \
             mpirun -np 4 ./Castro3d.gnu.MPI.CUDA.ex inputs.3d

# GPU-only (closest to PInsight CUDA domain only)
nsys profile --trace=cuda \
             --output=/tmp/castro_nsight_cuda \
             mpirun -np 4 ./Castro3d.gnu.MPI.CUDA.ex inputs.3d

# Minimal (Nsight overhead floor — kernel timing only)
nsys profile --trace=cuda --cuda-memory-usage=false \
             --output=/tmp/castro_nsight_min \
             mpirun -np 4 ./Castro3d.gnu.MPI.CUDA.ex inputs.3d
```

### PInsight run commands

```bash
# Full trace
LD_PRELOAD=/path/to/libpinsight.so \
mpirun -np 4 ./Castro3d.gnu.MPI.CUDA.ex inputs.3d

# Rate-limited (configured via CUDA_trace_config.install)
# [CUDA.global] max_num_traces = 50 tracing_rate = 1 trace_mode_after = MONITORING
LD_PRELOAD=/path/to/libpinsight.so \
mpirun -np 4 ./Castro3d.gnu.MPI.CUDA.ex inputs.3d

# MONITORING only (near-zero overhead, no tracepoints fired in CUDA domain)
# [CUDA.global] trace_mode = MONITORING
LD_PRELOAD=/path/to/libpinsight.so \
mpirun -np 4 ./Castro3d.gnu.MPI.CUDA.ex inputs.3d
```

### Metrics to report

| Metric | Unit | How to measure |
|---|---|---|
| Wall time | seconds | `time mpirun ...` (10-run average) |
| Overhead % | % vs baseline | `(tool_time - baseline) / baseline * 100` |
| Trace size | MB | `du -sh <trace_dir>` |
| GPU idle % | % | From Nsight GPU utilization; babeltrace2 for PInsight |

### Expected outcome narrative for paper

```
Table X: Overhead comparison — Castro Sedov 3D, 4×A100, 100 timesteps

Config                      | Wall time | Overhead | Trace size
Baseline                    |  X.X s    |   0%     |    —
PInsight MONITORING         |  X.X s    |  <1%     |   ~0 MB (no tracepoints)
PInsight Rate-limited (50)  |  X.X s    |  <5%     |  ~10 MB
PInsight Full               |  X.X s    |   ~X%    |  ~XX MB
Nsight GPU-only             |  X.X s    |  ~10–20% |  ~XXX MB
Nsight Full                 |  X.X s    |  ~20–30% |  ~XXX MB

Key claim: PInsight rate-limited achieves comparable insight to Nsight GPU-only
at <1/4 the overhead and 10× smaller trace volume, with the additional capability
of in-situ introspection unavailable in Nsight.
```

### Note on trace content comparison

Nsight and PInsight are not fully equivalent even in "full" mode:
- Nsight captures hardware GPU metrics (SM util, memory BW util) that PInsight does not
- PInsight captures OpenMP and MPI events in the same trace that Nsight does not
- For overhead comparison purposes, compare CUDA-only configs on both sides

---

## 9. CUPTI Exclusivity Warning for Evaluation

CUPTI allows **only one subscriber at a time**.  Running PInsight and Nsight simultaneously
is not possible.  For overhead comparison, run each tool in a separate job on a clean node.
Also verify no other CUPTI-based tools (Score-P with CUPTI, HPCToolkit GPU) are loaded.

```bash
# Check for conflicting preloads before running
echo $LD_PRELOAD
lsof -p $(pgrep Castro) | grep -i cupti   # should show only one tool's libcupti
```
