#!/bin/bash
# PInsight Application Knob Benchmark — LULESH 2.0
# Tests per-region num_threads control on a 48-core AMD EPYC
# Usage: bash run_knob_bench.sh [size] [iterations] [runs]

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
APP_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
SOURCE_DIR="$APP_DIR/source"
PINSIGHT_DIR="$(cd "$APP_DIR/../.." && pwd)"
RESULTS_ROOT="$APP_DIR/results/$(hostname -s)"
PINSIGHT_LIB="$PINSIGHT_DIR/build/libpinsight.so"
BASELINE_BIN="$SOURCE_DIR/lulesh2.0_baseline"
KNOB_BIN="$SOURCE_DIR/lulesh2.0"
S="${1:-50}"
ITERS="${2:-100}"
R="${3:-5}"
mkdir -p "$RESULTS_ROOT"
OUTFILE="$RESULTS_ROOT/knob_bench_results_$(date +%Y%m%d_%H%M%S).txt"

# Region classifications
HEAVY_REGIONS="integrate_stress_elem hourglass_force hourglass_control kinematics monotonic_q_grad monotonic_q_region"
MEDIUM_REGIONS="integrate_stress_node hourglass_node pressure_calc energy_compress energy_q_full energy_q_final sound_speed eos_alloc material_props courant_constraint hydro_constraint"
LIGHT_REGIONS="init_stress volume_force_determ force_nodes_init acceleration accel_boundary velocity position lagrange_elem_error pressure_bvc energy_init energy_q_half eos_region update_volumes"

max() { echo $(( $1 > $2 ? $1 : $2 )); }

# Generate a knob config file with specified H/M/L thread counts
gen_knob_config() {
    local h=$1 m=$2 l=$3 outfile=$4
    cat > "$outfile" <<EOF
# Auto-generated knob config: Heavy=$h, Medium=$m, Light=$l
[Knob]
EOF
    for r in $HEAVY_REGIONS; do
        printf "    %-35s = %3d    # H\n" "$r" "$h" >> "$outfile"
    done
    for r in $MEDIUM_REGIONS; do
        printf "    %-35s = %3d    # M\n" "$r" "$m" >> "$outfile"
    done
    for r in $LIGHT_REGIONS; do
        printf "    %-35s = %3d    # L\n" "$r" "$l" >> "$outfile"
    done
}

# Parse FOM and elapsed time from LULESH output
get_fom() { grep 'FOM' /tmp/lulesh_knob_out.txt | grep -oP '[\d.]+(?= \(z/s\))'; }
get_time() { grep 'Elapsed time' /tmp/lulesh_knob_out.txt | grep -oP '[\d.]+(?= \(s\))'; }

# Run a benchmark configuration — collects all runs, prints averages
run_config() {
    local label=$1; shift
    printf "%-35s" "$label:" | tee -a "$OUTFILE"
    local fom_vals=()
    local time_vals=()
    for i in $(seq 1 $R); do
        "$@" > /tmp/lulesh_knob_out.txt 2>/dev/null
        local t=$(get_time)
        local f=$(get_fom)
        printf "%12s" "$t" | tee -a "$OUTFILE"
        fom_vals+=("$f")
        time_vals+=("$t")
    done
    # Compute averages with awk
    local avg_fom=$(printf '%s\n' "${fom_vals[@]}" | awk '{s+=$1} END{printf "%.2f", s/NR}')
    local avg_time=$(printf '%s\n' "${time_vals[@]}" | awk '{s+=$1} END{printf "%.4f", s/NR}')
    printf "  FOM_avg=%-12s Time_avg=%s\n" "$avg_fom" "$avg_time" | tee -a "$OUTFILE"
}

# Compute S^3
ELEMENTS=$(( S * S * S ))

# Print header
{
echo "================================================================"
echo "PInsight Application Knob Benchmark — LULESH 2.0"
echo "Date: $(date)"
echo "Machine: $(hostname)"
echo "CPU: $(lscpu | grep 'Model name' | sed 's/.*: *//')"
echo "Cores: $(nproc) HW threads, 48 physical"
echo "Problem: -s $S -i $ITERS ($ELEMENTS elements)"
echo "Runs per config: $R"
echo "PInsight lib: $PINSIGHT_LIB"
echo "================================================================"
echo ""
} | tee "$OUTFILE"

THREAD_COUNTS="1 4 8 16 24 32 48"

for T in $THREAD_COUNTS; do
    # Compute tuned thread counts
    H=$T
    M=$(max 2 $((T / 2)))
    L=$(max 2 $((T / 4)))

    {
    echo "--- OMP_NUM_THREADS=$T  (Tuned: H=$H, M=$M, L=$L) ---"
    printf "%-35s" ""
    for i in $(seq 1 $R); do printf "%12s" "run$i"; done
    echo "  FOM_avg        Time_avg"
    echo "------------------------------------------------------------------------------------------------------"
    } | tee -a "$OUTFILE"

    # 1. Baseline: original LULESH, uniform thread count
    run_config "BASELINE (uniform ${T}T)" \
        env OMP_NUM_THREADS=$T OMP_TOOL=disabled "$BASELINE_BIN" -s $S -i $ITERS

    # 2. Knob uniform: all knobs = T (measures pure overhead)
    UNIFORM_CFG="/tmp/knob_uniform_${T}.txt"
    gen_knob_config $T $T $T "$UNIFORM_CFG"
    run_config "KNOB_UNIFORM (all=${T}T)" \
        env OMP_NUM_THREADS=$T PINSIGHT_TRACE_CONFIG_FILE="$UNIFORM_CFG" \
            PINSIGHT_TRACE_OPENMP=OFF OMP_TOOL_LIBRARIES="$PINSIGHT_LIB" \
            "$KNOB_BIN" -s $S -i $ITERS

    # 3. Knob tuned: H/M/L proportional
    TUNED_CFG="/tmp/knob_tuned_${T}.txt"
    gen_knob_config $H $M $L "$TUNED_CFG"
    run_config "KNOB_TUNED (H=${H}/M=${M}/L=${L})" \
        env OMP_NUM_THREADS=$T PINSIGHT_TRACE_CONFIG_FILE="$TUNED_CFG" \
            PINSIGHT_TRACE_OPENMP=OFF OMP_TOOL_LIBRARIES="$PINSIGHT_LIB" \
            "$KNOB_BIN" -s $S -i $ITERS

    echo "" | tee -a "$OUTFILE"
done

{
echo "================================================================"
echo "Done. Results saved to: $OUTFILE"
echo "================================================================"
} | tee -a "$OUTFILE"
