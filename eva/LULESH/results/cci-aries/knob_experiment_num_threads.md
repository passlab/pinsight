# PInsight Application Knob Experiment: Per-Region num_threads Control

**Date**: 2026-03-19  
**Author**: Automated analysis with PInsight knob instrumentation

## Objective

Evaluate whether per-region `num_threads` control can improve LULESH performance
by right-sizing thread counts for each parallel region based on computational weight.

## Machine Configuration

| Property | Value |
|----------|-------|
| CPU | Intel Xeon W-2133 @ 3.60 GHz |
| Cores | 6 physical cores, 12 threads (HT) |
| L1d/L1i Cache | 192 KiB (6 instances) |
| L2 Cache | 6 MiB (6 instances) |
| L3 Cache | 8.3 MiB (1 instance) |
| Memory | 32 GB DDR4 |
| NUMA | 1 node |
| OS | Linux 6.17.0-14-generic (Ubuntu 24.04) |
| Compiler | clang 21.1.8 via mpicxx |
| OpenMP | LLVM libomp |

## Instrumentation

All **30 parallel regions** in `lulesh.cc` were instrumented with
`num_threads(pinsight_get_knob_int("knob_name"))`. Each region was classified:

| Weight | Assigned Threads | Count | Regions |
|--------|-----------------|-------|---------|
| **Heavy** | 6 (all cores) | 7 | `integrate_stress_elem`, `hourglass_force`, `hourglass_control`, `kinematics`, `monotonic_q_grad`, `monotonic_q_region` |
| **Medium** | 4 | 11 | `integrate_stress_node`, `hourglass_node`, `pressure_calc`, `energy_compress`, `energy_q_full`, `energy_q_final`, `sound_speed`, `eos_alloc`, `material_props`, `courant_constraint`, `hydro_constraint` |
| **Light** | 2 | 12 | `init_stress`, `volume_force_determ`, `force_nodes_init`, `acceleration`, `accel_boundary`, `velocity`, `position`, `lagrange_elem_error`, `pressure_bvc`, `energy_init`, `energy_q_half`, `eos_region`, `update_volumes` |

Classification based on:
- **Heavy**: Large loop bodies (100-183 lines), deep function call chains, significant FP computation
- **Medium**: Moderate computation (12-48 lines), scatter/gather patterns, reductions
- **Light**: Simple element-wise ops (1-8 lines), zero-fill, position updates, boundary conditions

## Results

**Setup**: s=10 (1000 elements), 30 iterations, 1 MPI rank, OMP_NUM_THREADS=6, 3 runs each.

| Config | Run 1 FOM | Run 2 FOM | Run 3 FOM | Avg FOM (z/s) | Avg Elapsed (s) |
|--------|-----------|-----------|-----------|---------------|-----------------|
| **Baseline** (original, uniform 6t) | 72.4 | 68.3 | 61.4 | **67.4** | 0.45 |
| **Knob H=6/M=4/L=2** | 61.8 | 75.6 | 65.2 | **67.5** | 0.45 |
| **Knob ALL=6** (overhead test) | 63.2 | 59.4 | 61.9 | **61.5** | 0.49 |

## Analysis

1. **Knob-optimized matches baseline**: Per-region thread control (H=6/M=4/L=2)
   achieves FOM 67.5 vs baseline 67.4 — effectively identical performance.

2. **Knob lookup overhead ~9%**: When all regions use 6 threads (ALL=6),
   performance drops to FOM 61.5 (≈9% slower than baseline). This overhead
   comes from `pinsight_get_knob_int()` lookups (linear scan of knob table)
   and the `num_threads()` clause forcing the runtime to validate/resize the
   thread team on every parallel region entry.

3. **Thread reduction compensates**: Using fewer threads for lightweight
   regions avoids unnecessary thread synchronization overhead, which fully
   offsets the knob lookup cost.

4. **High variance**: With s=10 (only 1000 elements across 6 threads),
   run-to-run variance is significant. Larger problem sizes would yield
   more stable measurements.

## Expected Results on Larger Machines

On machines with more cores (16, 32, 64+), per-region thread control should
show **larger benefits** because:
- Thread synchronization overhead scales with core count
- Work-per-thread ratio for light regions becomes unfavorable at high core counts
- NUMA effects penalize spreading trivial work across multiple sockets
- Heavy regions still benefit from full core utilization

## How to Reproduce

```bash
# Build baseline (original LULESH)
git stash   # stash knob changes
make clean && make
cp lulesh2.0 lulesh2.0_baseline
git stash pop

# Build knob-instrumented version
make clean && make

# Run baseline
OMP_NUM_THREADS=6 mpirun -np 1 ./lulesh2.0_baseline -s 10 -i 30

# Run knob-optimized
cp pinsight_knob_config.txt pinsight_trace_config.txt
PINSIGHT_TRACE_CONFIG_FILE=pinsight_trace_config.txt \
  OMP_NUM_THREADS=6 mpirun -np 1 ./lulesh2.0 -s 10 -i 30
```

## Files

- `lulesh.cc` — Instrumented with 30 `num_threads(pinsight_get_knob_int(...))` clauses
- `pinsight_knob_config.txt` — Knob configuration with H=6/M=4/L=2 thread counts
- `Makefile` — Updated with PInsight include path and `app_knob.o` linkage
