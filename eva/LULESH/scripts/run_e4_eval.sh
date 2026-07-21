#!/bin/bash
# =============================================================================
# run_e4_eval.sh — E4 Introspection + Adaptive Knob Tuning Evaluation
# =============================================================================
# Problem size: 32^3, validated to show per-region tuning benefit
# Threads: 24, bound to NUMA nodes 0-3 (24 physical cores, one socket)
# Configs tested:
#   1. Baseline       — original LULESH (no pinsight, no knobs)
#   2. Uniform-24     — all knobs = 24 (no introspection)
#   3. Static-tuned   — manual H/M/L knob assignment (no introspection)
#   4. Auto-tuned     — uniform start → INTROSPECT → knob tuning
# =============================================================================

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
APP_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
SOURCE_DIR="$APP_DIR/source"
PINSIGHT_DIR="$(cd "$APP_DIR/../.." && pwd)"
RESULTS_ROOT="$APP_DIR/results/$(hostname -s)"
RESULTS_DIR="${RESULTS_ROOT}/results_e4"
LULESH="${SOURCE_DIR}/lulesh2.0"
LIBPINSIGHT="${PINSIGHT_DIR}/build/libpinsight.so"
ANALYZE_SCRIPT="${SCRIPT_DIR}/analyze_and_tune.sh"

PROBLEM_SIZE=${SIZE:-32}
NUM_THREADS=${THREADS:-24}
NUM_ITER=${ITER:-1000}
NUM_RUNS=${RUNS:-4}

echo "========================================================"
echo "  E4 Evaluation: Introspection + Adaptive Knob Tuning"
echo "========================================================"
echo "  Problem size: ${PROBLEM_SIZE}^3"
echo "  Threads: $NUM_THREADS (numactl bound)"
echo "  Iterations: $NUM_ITER"
echo "  Runs per config: $NUM_RUNS"
echo "  Results: $RESULTS_DIR"
echo "========================================================"
mkdir -p "$RESULTS_DIR"

# Helper: run LULESH and extract FOM
run_lulesh() {
    local label="$1"
    local run_num="$2"
    shift 2
    # "$@" = env vars passed before lulesh binary
    local out_file="${RESULTS_DIR}/${label}_run${run_num}.txt"
    # Bind to NUMA nodes 0-3 (physical cores 0-23, one socket, 24 cores)
    numactl --cpunodebind=0-3 --membind=0-3 \
        env OMP_PLACES=cores OMP_PROC_BIND=close "$@" \
        "${LULESH}" -s "$PROBLEM_SIZE" -i "$NUM_ITER" > "$out_file" 2>&1
    local fom=$(grep "FOM" "$out_file" | awk '{print $NF}')
    local elapsed=$(grep "Elapsed time" "$out_file" | awk '{print $NF}')
    echo "    Run $run_num: FOM=$fom  Elapsed=${elapsed}s"
    echo "$fom"
}

# ---- Config 1: Baseline ----
echo ""
echo "--- Config 1: Baseline (no PInsight) ---"
BASELINE_FOMS=()
for i in $(seq 1 $NUM_RUNS); do
    fom=$(run_lulesh "baseline" "$i" \
        env OMP_NUM_THREADS=$NUM_THREADS \
            LD_PRELOAD="libtbbmalloc_proxy.so")
    BASELINE_FOMS+=($fom)
done

# ---- Config 2: Uniform-24 (knobs, no introspection) ----
echo ""
echo "--- Config 2: PInsight Uniform-$NUM_THREADS (all knobs = $NUM_THREADS) ---"
UNIFORM_FOMS=()
for i in $(seq 1 $NUM_RUNS); do
    cp "${SCRIPT_DIR}/e4_introspect_uniform.cfg" "${RESULTS_DIR}/uniform_cfg_run${i}.cfg"
    # Remove INTROSPECT from mode_after — just knobs, no pausing
    sed -i 's/trace_mode_after.*/trace_mode_after = STANDBY/' "${RESULTS_DIR}/uniform_cfg_run${i}.cfg"
    # Also disable tracing to measure pure knob overhead
    sed -i 's/trace_mode = TRACING/trace_mode = STANDBY/' "${RESULTS_DIR}/uniform_cfg_run${i}.cfg"
    fom=$(run_lulesh "uniform" "$i" \
        env OMP_NUM_THREADS=$NUM_THREADS \
            OMP_TOOL_LIBRARIES="$LIBPINSIGHT" \
            LD_PRELOAD="libtbbmalloc_proxy.so" \
            PINSIGHT_TRACE_CONFIG_FILE="${RESULTS_DIR}/uniform_cfg_run${i}.cfg")
    UNIFORM_FOMS+=($fom)
done

# ---- Config 3: Static-tuned (optimal known knobs, no introspection) ----
echo ""
echo "--- Config 3: PInsight Static-Tuned (H=${NUM_THREADS}/M=$(( NUM_THREADS*2/3 ))/L=$(( NUM_THREADS/3 ))) ---"
STATIC_CFG="${RESULTS_DIR}/static_tuned.cfg"
cp "${SCRIPT_DIR}/e4_introspect_uniform.cfg" "$STATIC_CFG"
sed -i 's/trace_mode_after.*/trace_mode_after = STANDBY/' "$STATIC_CFG"
sed -i 's/trace_mode = TRACING/trace_mode = STANDBY/' "$STATIC_CFG"
# Apply optimal knobs (same as analyze_and_tune.sh computes)
HEAVY_T=$NUM_THREADS
# Match analyze_and_tune.sh: Medium=2/3 max, Light=1/3 max
MED_T=$(( NUM_THREADS * 2 / 3 ))
LIGHT_T=$(( NUM_THREADS / 3 ))
[ $LIGHT_T -lt 2 ] && LIGHT_T=2
# Heavy
for k in integrate_stress_elem hourglass_force hourglass_control kinematics monotonic_q_grad monotonic_q_region; do
    sed -i "s/^\(\s*${k}\s*=\s*\)[0-9]*/\1${HEAVY_T}/" "$STATIC_CFG"
done
# Medium
for k in integrate_stress_node hourglass_node pressure_calc energy_compress energy_q_full energy_q_final sound_speed eos_alloc material_props courant_constraint hydro_constraint; do
    sed -i "s/^\(\s*${k}\s*=\s*\)[0-9]*/\1${MED_T}/" "$STATIC_CFG"
done
# Light
for k in init_stress volume_force_determ force_nodes_init acceleration accel_boundary velocity position lagrange_elem_error pressure_bvc energy_init energy_q_half eos_region update_volumes; do
    sed -i "s/^\(\s*${k}\s*=\s*\)[0-9]*/\1${LIGHT_T}/" "$STATIC_CFG"
done

STATIC_FOMS=()
for i in $(seq 1 $NUM_RUNS); do
    fom=$(run_lulesh "static" "$i" \
        env OMP_NUM_THREADS=$NUM_THREADS \
            OMP_TOOL_LIBRARIES="$LIBPINSIGHT" \
            LD_PRELOAD="libtbbmalloc_proxy.so" \
            PINSIGHT_TRACE_CONFIG_FILE="$STATIC_CFG")
    STATIC_FOMS+=($fom)
done

# ---- Config 4: Auto-tuned (INTROSPECT → analyze_and_tune.sh) ----
echo ""
echo "--- Config 4: PInsight Auto-Tuned (INTROSPECT + knob tuning) ---"
AUTOTUNED_FOMS=()
for i in $(seq 1 $NUM_RUNS); do
    AUTO_CFG="${RESULTS_DIR}/autotuned_cfg_run${i}.cfg"
    cp "${SCRIPT_DIR}/e4_introspect_uniform.cfg" "$AUTO_CFG"
    # Use absolute path for introspect script, 60s pause timeout
    sed -i "s|INTROSPECT:[0-9]*:[^:]*|INTROSPECT:60:${ANALYZE_SCRIPT}|" "$AUTO_CFG"

    fom=$(run_lulesh "autotuned" "$i" \
        env OMP_NUM_THREADS=$NUM_THREADS \
            OMP_TOOL_LIBRARIES="$LIBPINSIGHT" \
            LD_PRELOAD="libtbbmalloc_proxy.so" \
            PINSIGHT_TRACE_CONFIG_FILE="$AUTO_CFG")
    AUTOTUNED_FOMS+=($fom)
    # Capture the tuned config for inspection
    cp "$AUTO_CFG" "${RESULTS_DIR}/autotuned_after_run${i}.cfg"
done

# ---- Summary ----
echo ""
echo "========================================================"
echo "  E4 Results Summary"
echo "========================================================"

avg_fom() {
    # Use awk instead of bc (bc may not be installed)
    printf '%s\n' "$@" | awk '{sum+=$1; n++} END{printf "%.1f", sum/n}'
}

pct_diff() {
    # $1=value $2=reference; positive = faster
    awk -v v="$1" -v r="$2" 'BEGIN{printf "%+.1f", (v-r)/r*100}'
}

BASE_AVG=$(avg_fom "${BASELINE_FOMS[@]}")
UNIF_AVG=$(avg_fom "${UNIFORM_FOMS[@]}")
STAT_AVG=$(avg_fom "${STATIC_FOMS[@]}")
AUTO_AVG=$(avg_fom "${AUTOTUNED_FOMS[@]}")

printf "\n%-22s %10s %12s\n" "Configuration" "Avg FOM" "vs Baseline"
printf "%-22s %10s %12s\n"   "---" "---" "---"
printf "%-22s %10.1f %12s\n" "Baseline (TBB)" "$BASE_AVG" "—"
printf "%-22s %10.1f %11s%%\n" "Uniform-$NUM_THREADS" "$UNIF_AVG" "$(pct_diff $UNIF_AVG $BASE_AVG)"
printf "%-22s %10.1f %11s%%\n" "Static H/M/L" "$STAT_AVG" "$(pct_diff $STAT_AVG $BASE_AVG)"
printf "%-22s %10.1f %11s%%\n" "Auto-tuned" "$AUTO_AVG" "$(pct_diff $AUTO_AVG $BASE_AVG)"

echo ""
echo "Results saved to: $RESULTS_DIR"

# Save summary
{
    echo "=== E4 Summary ==="
    echo "Date: $(date)"
    echo "Size: ${PROBLEM_SIZE}^3  Threads: $NUM_THREADS  Iter: $NUM_ITER  Runs: $NUM_RUNS"
    echo "Ratios: H=${HEAVY_T}T / M=${MED_T}T / L=${LIGHT_T}T"
    echo ""
    printf "%-22s %10s %12s\n" "Configuration" "Avg FOM" "vs Baseline"
    printf "%-22s %10.1f %12s\n" "Baseline (TBB)" "$BASE_AVG" "—"
    printf "%-22s %10.1f %11s%%\n" "Uniform-$NUM_THREADS" "$UNIF_AVG" "$(pct_diff $UNIF_AVG $BASE_AVG)"
    printf "%-22s %10.1f %11s%%\n" "Static H/M/L" "$STAT_AVG" "$(pct_diff $STAT_AVG $BASE_AVG)"
    printf "%-22s %10.1f %11s%%\n" "Auto-tuned" "$AUTO_AVG" "$(pct_diff $AUTO_AVG $BASE_AVG)"
} | tee "$RESULTS_DIR/e4_summary.txt"
