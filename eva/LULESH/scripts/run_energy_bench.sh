#!/bin/bash
# Energy analysis: Baseline vs Knob-Tuned (L=2) at T=24
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
T=24
mkdir -p "$RESULTS_ROOT"
OUTFILE="$RESULTS_ROOT/energy_results_$(date +%Y%m%d_%H%M%S).txt"

# Region classifications
HEAVY_REGIONS="integrate_stress_elem hourglass_force hourglass_control kinematics monotonic_q_grad monotonic_q_region"
MEDIUM_REGIONS="integrate_stress_node hourglass_node pressure_calc energy_compress energy_q_full energy_q_final sound_speed eos_alloc material_props courant_constraint hydro_constraint"
LIGHT_REGIONS="init_stress volume_force_determ force_nodes_init acceleration accel_boundary velocity position lagrange_elem_error pressure_bvc energy_init energy_q_half eos_region update_volumes"

gen_knob_config() {
    local h=$1 m=$2 l=$3 outfile=$4
    cat > "$outfile" <<EOF
[Knob]
EOF
    for r in $HEAVY_REGIONS; do printf "    %-35s = %3d\n" "$r" "$h" >> "$outfile"; done
    for r in $MEDIUM_REGIONS; do printf "    %-35s = %3d\n" "$r" "$m" >> "$outfile"; done
    for r in $LIGHT_REGIONS; do printf "    %-35s = %3d\n" "$r" "$l" >> "$outfile"; done
}

{
echo "================================================================"
echo "Energy Analysis — LULESH 2.0 at T=$T"
echo "Date: $(date)"
echo "Machine: $(hostname)"
echo "CPU: $(lscpu | grep 'Model name' | sed 's/.*: *//')"
echo "Problem: -s $S -i $ITERS ($(( S*S*S )) elements)"
echo "Runs per config: $R"
echo "Configs: Baseline (uniform 24T), Tuned (H=24/M=12/L=2)"
echo "Energy: perf stat -e power/energy-pkg/ (RAPL, both packages)"
echo "================================================================"
echo ""
} | tee "$OUTFILE"

# Generate tuned config
TUNED_CFG="/tmp/knob_tuned_energy_24.txt"
gen_knob_config 24 12 2 "$TUNED_CFG"

for config in "baseline" "tuned"; do
    echo "=== Configuration: $config ===" | tee -a "$OUTFILE"
    for i in $(seq 1 $R); do
        echo "--- Run $i/$R ---" | tee -a "$OUTFILE"
        PERF_OUT="/tmp/perf_energy_${config}_${i}.txt"
        if [ "$config" = "baseline" ]; then
            perf stat -e power/energy-pkg/ -a -o "$PERF_OUT" -- \
                env OMP_NUM_THREADS=$T OMP_TOOL=disabled "$BASELINE_BIN" -s $S -i $ITERS 2>&1 | \
                grep -E 'FOM|Elapsed' | tee -a "$OUTFILE"
        else
            perf stat -e power/energy-pkg/ -a -o "$PERF_OUT" -- \
                env OMP_NUM_THREADS=$T PINSIGHT_TRACE_CONFIG_FILE="$TUNED_CFG" \
                    PINSIGHT_TRACE_OPENMP=OFF OMP_TOOL_LIBRARIES="$PINSIGHT_LIB" \
                    "$KNOB_BIN" -s $S -i $ITERS 2>&1 | \
                grep -E 'FOM|Elapsed' | tee -a "$OUTFILE"
        fi
        echo "  Energy:" | tee -a "$OUTFILE"
        grep -E 'Joules|seconds time' "$PERF_OUT" | sed 's/^/    /' | tee -a "$OUTFILE"
        echo "" | tee -a "$OUTFILE"
    done
done

echo "================================================================" | tee -a "$OUTFILE"
echo "Done. Results saved to: $OUTFILE" | tee -a "$OUTFILE"
echo "================================================================" | tee -a "$OUTFILE"
