#!/bin/bash
# Benchmark: trace-guided knob config at T=48 with energy measurement
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
T=48
mkdir -p "$RESULTS_ROOT"
OUTFILE="$RESULTS_ROOT/trace_guided_results_$(date +%Y%m%d_%H%M%S).txt"

# Trace-guided knob config: based on measured %time at T=48
GUIDED_CFG="/tmp/knob_trace_guided_48.txt"
cat > "$GUIDED_CFG" << 'EOF'
# Trace-guided knob config: H=48 (≥5%), M=24 (1-5%), L=2 (<1%)
[Knob]
    integrate_stress_elem               =  48    # H (10.2%)
    integrate_stress_node               =  48    # H (9.6%)
    hourglass_force                     =  48    # H (8.6%)
    eos_alloc                           =  48    # H (8.6%)
    kinematics                          =  48    # H (6.8%)
    hourglass_node                      =  48    # H (5.3%)
    monotonic_q_region                  =  24    # M (4.5%)
    init_stress                         =  24    # M (3.6%)
    energy_compress                     =  24    # M (3.3%)
    sound_speed                         =  24    # M (3.0%)
    position                            =  24    # M (2.9%)
    pressure_calc                       =  24    # M (2.8%)
    pressure_bvc                        =  24    # M (2.8%)
    volume_force_determ                 =  24    # M (2.6%)
    energy_q_final                      =  24    # M (2.6%)
    courant_constraint                  =  24    # M (2.6%)
    energy_init                         =  24    # M (2.5%)
    update_volumes                      =  24    # M (2.4%)
    energy_q_full                       =  24    # M (2.4%)
    eos_region                          =  24    # M (2.4%)
    energy_q_half                       =  24    # M (2.1%)
    monotonic_q_grad                    =  24    # M (1.4%)
    hydro_constraint                    =  24    # M (1.2%)
    velocity                            =   2    # L (0.8%)
    accel_boundary                      =   2    # L (0.8%)
    force_nodes_init                    =   2    # L (0.6%)
    lagrange_elem_error                 =   2    # L (0.5%)
    material_props                      =   2    # L (0.3%)
    hourglass_control                   =   2    # L (0.1%)
    acceleration                        =   2    # L (0.1%)
EOF

# Uniform 48T config for overhead comparison
UNIFORM_CFG="/tmp/knob_uniform_48.txt"
cat > "$UNIFORM_CFG" << 'EOF2'
[Knob]
    integrate_stress_elem               =  48
    integrate_stress_node               =  48
    hourglass_force                     =  48
    eos_alloc                           =  48
    kinematics                          =  48
    hourglass_node                      =  48
    monotonic_q_region                  =  48
    init_stress                         =  48
    energy_compress                     =  48
    sound_speed                         =  48
    position                            =  48
    pressure_calc                       =  48
    pressure_bvc                        =  48
    volume_force_determ                 =  48
    energy_q_final                      =  48
    courant_constraint                  =  48
    energy_init                         =  48
    update_volumes                      =  48
    energy_q_full                       =  48
    eos_region                          =  48
    energy_q_half                       =  48
    monotonic_q_grad                    =  48
    hydro_constraint                    =  48
    velocity                            =  48
    accel_boundary                      =  48
    force_nodes_init                    =  48
    lagrange_elem_error                 =  48
    material_props                      =  48
    hourglass_control                   =  48
    acceleration                        =  48
EOF2

{
echo "================================================================"
echo "Trace-Guided Knob Benchmark — LULESH 2.0 at T=$T"
echo "Date: $(date)"
echo "Machine: $(hostname)"
echo "Problem: -s $S -i $ITERS ($(( S*S*S )) elements)"
echo "Runs per config: $R"
echo "Configs: Baseline, Uniform (all=48), Trace-Guided (H=48/M=24/L=2)"
echo "Energy: perf stat -e power/energy-pkg/ (RAPL)"
echo "================================================================"
echo ""
} | tee "$OUTFILE"

for config in "baseline" "uniform" "trace_guided"; do
    echo "=== Configuration: $config ===" | tee -a "$OUTFILE"
    for i in $(seq 1 $R); do
        echo "--- Run $i/$R ---" | tee -a "$OUTFILE"
        PERF_OUT="/tmp/perf_tg_${config}_${i}.txt"
        if [ "$config" = "baseline" ]; then
            perf stat -e power/energy-pkg/ -a -o "$PERF_OUT" -- \
                env OMP_NUM_THREADS=$T OMP_TOOL=disabled "$BASELINE_BIN" -s $S -i $ITERS 2>&1 | \
                grep -E 'FOM|Elapsed' | tee -a "$OUTFILE"
        elif [ "$config" = "uniform" ]; then
            perf stat -e power/energy-pkg/ -a -o "$PERF_OUT" -- \
                env OMP_NUM_THREADS=$T PINSIGHT_TRACE_CONFIG_FILE="$UNIFORM_CFG" \
                    PINSIGHT_TRACE_OPENMP=OFF OMP_TOOL_LIBRARIES="$PINSIGHT_LIB" \
                    "$KNOB_BIN" -s $S -i $ITERS 2>&1 | \
                grep -E 'FOM|Elapsed' | tee -a "$OUTFILE"
        else
            perf stat -e power/energy-pkg/ -a -o "$PERF_OUT" -- \
                env OMP_NUM_THREADS=$T PINSIGHT_TRACE_CONFIG_FILE="$GUIDED_CFG" \
                    PINSIGHT_TRACE_OPENMP=OFF OMP_TOOL_LIBRARIES="$PINSIGHT_LIB" \
                    "$KNOB_BIN" -s $S -i $ITERS 2>&1 | \
                grep -E 'FOM|Elapsed' | tee -a "$OUTFILE"
        fi
        echo "  Energy:" | tee -a "$OUTFILE"
        grep -E 'Joules|seconds time' "$PERF_OUT" | sed 's/^/    /' | tee -a "$OUTFILE"
        echo "" | tee -a "$OUTFILE"
    done
done

{
echo "================================================================"
echo "Done. Results saved to: $OUTFILE"
echo "================================================================"
} | tee -a "$OUTFILE"
