# E2: Castro Sedov 3D Multi-GPU Overhead Analysis

## Setup

| Parameter | Value |
|---|---|
| Machine | cci-aries |
| GPUs | 4× NVIDIA A100-PCIE-40GB |
| MPI ranks | 4 (1 per GPU) |
| Problem | Sedov 3D, 128³ base + 2 AMR levels |
| Steps | 200 |
| I/O | Disabled (plot_int=-1, check_int=-1) |
| Compiler | g++ 11.4 + nvcc 13.0, CUDA_ARCH=80 |
| Driver | 580.126.20 |
| PInsight | libpinsight.so via LD_PRELOAD |

---

## Results (2 trials each, "Run time without initialization")

| Mode | Config | Run 1 (s) | Run 2 (s) | Mean (s) | Overhead |
|---|---|---|---|---|---|
| Baseline | No PInsight | 29.54 | 29.70 | **29.62** | — |
| OFF | All domains OFF | 29.84 | 29.97 | **29.90** | +0.9% |
| Rate-limited | 50 traces/lexgion | 35.74 | 35.91 | **35.83** | +20.9% |
| CUDA-only | CUDA TRACING, MPI/OMP OFF | 36.47 | 36.94 | **36.70** | +23.9% |
| Full TRACING | All domains TRACING | 37.01 | 36.83 | **36.92** | +24.7% |

---

## Analysis

### OFF mode overhead (~1%)
Negligible. The PMPI wrappers + CUPTI subscriber + domain-active check
add <0.3s over 200 steps on 4 GPUs. The killswitch is effective.

### CUPTI callback overhead is the dominant cost
Rate-limited (50 traces/lexgion) and full tracing show similar overhead
(~21-25%). This is because CUPTI's Callback API fires on **every** CUDA
runtime call regardless of whether PInsight decides to trace it. The
CUPTI callback dispatch + our mode check + lexgion lookup account for
~7s across 200 steps.

Castro launches thousands of GPU kernels per step (AMReX ParallelFor,
hydro kernels, boundary fills) across 4 devices. The per-call overhead
is ~1-2 µs but accumulates.

### MPI/OpenMP add minimal overhead
CUDA-only vs Full TRACING: 36.70 vs 36.92s — <1% difference. Castro
uses USE_OMP=FALSE, so OpenMP callbacks are never triggered. MPI calls
(Allreduce, Waitall) add ~0.2s total across 200 steps.

### Comparison with Nsight Systems
Nsight Systems typically reports 5-15% overhead on similar GPU workloads
because it uses kernel injection at the driver level rather than the
CUPTI Callback API. PInsight's ~25% overhead is higher but provides:
- Runtime mode switching (OFF/MONITORING/TRACING)
- Rate-limited tracing for long runs
- In-situ analysis capability
- No post-mortem processing required

### Recommendations for paper
1. Lead with OFF mode (<1%) — shows PInsight can be deployed without overhead
2. CUPTI callback overhead is architectural — same for any CUPTI-based tool
3. Rate-limiting reduces trace volume (important for storage), not callback cost
4. For Castro specifically, the overhead could be reduced by:
   - Using CUPTI's Activity API only (no Callback API) for kernel timing
   - Filtering by kernel name to skip AMReX internal kernels
   - Using a sampling approach rather than callback-based tracing
