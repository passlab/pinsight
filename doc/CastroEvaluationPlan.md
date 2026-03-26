# PInsight Evaluation Plan: Castro Astrophysics Code

## Goal

Demonstrate PInsight's value for **performance tuning and optimization** of a real-world production simulation code using three complementary experiments.

## Castro Overview

Castro is an AMR astrophysics code (MPI + tiled-OpenMP or MPI + GPU via AMReX).

**Key architecture points:**
- OpenMP and GPU are **mutually exclusive** build configs (cannot use both)
- OpenMP uses AMReX's **tiled MFIter model**: `#pragma omp parallel` wraps a `for (MFIter mfi(...); ...)` loop over grid patches
- GPU version uses `amrex::ParallelFor` with CUDA/HIP lambda kernels
- 33 source files with 100+ `#pragma omp parallel` regions
- Physics modules: hydro, gravity, radiation, reactions, MHD, diffusion

**Test problems:**
- **Sedov blast** (`Exec/hydro_tests/Sedov`): Pure hydro, no external deps, simplest build
- **Reacting bubble** (`Exec/reacting_tests/reacting_bubble`): Hydro + nuclear reactions
- **Toy flame** (`Exec/reacting_tests/toy_flame`): Combustion problem with stiff reactions

**Machine:** Intel Xeon W-2133 (6 cores), NVIDIA GPU (CUDA 13.0), 32 GB RAM

---

## Demo 1: Selective OpenMP Tracing → Bottleneck Identification

### Objective
Show how PInsight's selective tracing identifies performance hotspots with minimal overhead, enabling targeted optimization.

### Experiment
1. Build Sedov 3D with `USE_OMP=TRUE, USE_MPI=FALSE` (pure OpenMP)
2. Run baseline (no PInsight) — record wall time
3. Run with PInsight full tracing — measure trace volume and overhead
4. Run with PInsight **selective tracing** (rate-limited, hydro events only) — show dramatically reduced overhead while retaining critical hot-path data
5. Analyze traces to identify which OpenMP regions dominate execution time

### PInsight Features Demonstrated
- `[Domain.OpenMP]` event on/off filtering
- `[Lexgion.default]` rate-limited tracing (`trace_starts_at`, `tracing_rate`)
- Per-address lexgion config for high-interest regions
- Overhead comparison: full vs selective vs no tracing

### Expected Outcome
- Identify the hydro reconstruct/Riemann solve as the dominant cost
- Show that selective tracing captures the essential performance data with <5% overhead vs 20-30% for full tracing

---

## Demo 2: Rate-Limited Tracing + PAUSE → Iterative Tuning Workflow  

### Objective
Demonstrate the PAUSE feature for an adaptive "trace → analyze → adjust → resume" workflow in a multi-phase simulation.

### Experiment
1. Build **reacting_bubble** or **toy_flame** with `USE_OMP=TRUE`
2. Configure PInsight with:
   ```
   [Lexgion.default]
       max_num_traces = 500
       trace_mode_after = PAUSE:30:analyze_traces.sh:MONITORING
   ```
3. Run the simulation:
   - **Phase 1**: PInsight traces the first 500 parallel region executions (initial timesteps)
   - **Phase 2**: PAUSE — PInsight triggers `analyze_traces.sh` which summarizes per-region timing
   - **Phase 3**: Resume in MONITORING mode — lightweight sampling for the rest of the run
4. Show the analysis script output identifying performance anomalies
5. Optionally: SIGUSR1 reload with updated config for a second tracing burst

### PInsight Features Demonstrated  
- Rate-limited tracing with `max_num_traces`
- **PAUSE with script execution** (`trace_mode_after = PAUSE:timeout:script`)
- Mode switching (TRACING → PAUSE → MONITORING)
- Config reload via SIGUSR1

### Expected Outcome
- Show a complete "observe → analyze → adjust" loop without restarting the simulation
- Demonstrate how PInsight enables iterative tuning in long-running simulations

---

## Demo 3: Application Knob → Runtime Parameter Optimization

### Objective
Show how PInsight's application knob feature enables runtime tuning that improves performance or simulation quality.

### Experiment A: OpenMP Tile Size Tuning
Castro/AMReX's `hydro_tile_size` controls the tile granularity for OpenMP:
- Too small → thread overhead dominates
- Too large → poor load balance, cache pressure

1. Define knobs for tile sizes: `knob_tile_x`, `knob_tile_y`, `knob_tile_z`
2. Instrument Castro to use `pinsight_get_knob_int("knob_tile_x")` for tile size
3. Run Sedov 3D with different tile size configs (e.g., 8³ vs 16³ vs 32³ vs 64³)
4. Compare throughput (zone-updates/sec) across configurations
5. Show runtime tuning: start with 16³, SIGUSR1 to switch to optimal size mid-run

### Experiment B: AMR Regrid Interval Tuning  
Castro's `regrid_int` controls how often the adaptive mesh is re-computed:
- Small interval → better mesh adaptivity but higher overhead
- Large interval → lower overhead but potentially under-resolved regions

1. Define knob `knob_regrid_int`
2. Run reacting_bubble comparing different regrid intervals
3. Show impact on both performance and result quality (conservation errors)

### PInsight Features Demonstrated
- Application knobs for runtime-tunable parameters
- SIGUSR1 config reload for mid-run adjustments
- Performance vs. quality trade-off analysis

### Expected Outcome
- Identify optimal tile size for this machine's cache hierarchy
- Show measurable performance improvement from runtime tuning
- Demonstrate PInsight's unique value: runtime parameter exploration without code recompilation or job restart

---

## Build Plan

### Step 1: Build OpenMP Sedov
```bash
cd eva/Castro/Exec/hydro_tests/Sedov
# Edit GNUmakefile: USE_OMP=TRUE, USE_MPI=FALSE, COMP=llvm
make DIM=3 -j6
```

### Step 2: Integrate PInsight
- PInsight uses `LD_PRELOAD=libpinsight.so` — no source modifications needed for Demos 1-2
- Demo 3 requires minor source changes to read knob values

### Step 3: Run experiments
- Each demo runs independently
- Results documented in `eva/Castro/pinsight_evaluation/`

## Verification Plan

### Automated Tests
- Build verification: each configuration compiles cleanly
- Correctness: Castro's built-in conservation checks pass with PInsight active
- Performance: wall-time comparisons using Castro's internal timers

### Manual Verification
- Trace analysis using `babeltrace` for LTTng traces
- Visual inspection of PAUSE workflow output
- Knob tuning results comparison table
