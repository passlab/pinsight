#!/bin/bash
# =============================================================================
# analyze_and_tune.sh — PInsight In-Situ Knob Tuning
# =============================================================================
# Called by PInsight control thread: execlp(script, chunk, pid, config)
# =============================================================================
CHUNK_PATH=$1
APP_PID=$2
CONFIG_FILE=$3
# Resolve paths relative to this script's location ($0 is the absolute path
# passed by the PInsight control thread, or the script path when run standalone)
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
APP_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
RESULTS_ROOT="$APP_DIR/results/$(hostname -s)"
mkdir -p "$RESULTS_ROOT"
LOG="${RESULTS_ROOT}/e4_introspect_log_run4_run3_run2_run1_run2_run1.txt"
REPORT="${RESULTS_ROOT}/e4_introspect_report.txt"

# Write using direct file descriptors (avoids exec 2>> issues in fork context)
log() { echo "$*" >> "$LOG"; }
rpt() { echo "$*" >> "$REPORT"; }

log ""
log "=== PInsight In-Situ Knob Tuning === $(date)"
log "PID=$APP_PID  Config=$CONFIG_FILE  Chunk=$CHUNK_PATH"

MAX_T=${OMP_NUM_THREADS:-24}
HEAVY_T=$MAX_T
# Medium = 2/3 of max (rounds down); Light = 1/3 of max
# Validated: at 24T gives H=24/M=16/L=8 → +3.5% vs uniform-24 at s=32
MED_T=$(( MAX_T * 2 / 3 ))
LIGHT_T=$(( MAX_T / 3 ))
[ $MED_T -lt 4 ] && MED_T=4
[ $LIGHT_T -lt 2 ] && LIGHT_T=2

log "Thread plan: H=$HEAVY_T  M=$MED_T  L=$LIGHT_T (max=$MAX_T)"

# ---- Known LULESH region classification ----
declare -A KNOBS
# Heavy (6) — computation-intensive
KNOBS[integrate_stress_elem]=$HEAVY_T
KNOBS[hourglass_force]=$HEAVY_T
KNOBS[hourglass_control]=$HEAVY_T
KNOBS[kinematics]=$HEAVY_T
KNOBS[monotonic_q_grad]=$HEAVY_T
KNOBS[monotonic_q_region]=$HEAVY_T
# Medium (11) — moderate, scatter/gather
KNOBS[integrate_stress_node]=$MED_T
KNOBS[hourglass_node]=$MED_T
KNOBS[pressure_calc]=$MED_T
KNOBS[energy_compress]=$MED_T
KNOBS[energy_q_full]=$MED_T
KNOBS[energy_q_final]=$MED_T
KNOBS[sound_speed]=$MED_T
KNOBS[eos_alloc]=$MED_T
KNOBS[material_props]=$MED_T
KNOBS[courant_constraint]=$MED_T
KNOBS[hydro_constraint]=$MED_T
# Light (13) — simple element-wise
KNOBS[init_stress]=$LIGHT_T
KNOBS[volume_force_determ]=$LIGHT_T
KNOBS[force_nodes_init]=$LIGHT_T
KNOBS[acceleration]=$LIGHT_T
KNOBS[accel_boundary]=$LIGHT_T
KNOBS[velocity]=$LIGHT_T
KNOBS[position]=$LIGHT_T
KNOBS[lagrange_elem_error]=$LIGHT_T
KNOBS[pressure_bvc]=$LIGHT_T
KNOBS[energy_init]=$LIGHT_T
KNOBS[energy_q_half]=$LIGHT_T
KNOBS[eos_region]=$LIGHT_T
KNOBS[update_volumes]=$LIGHT_T

# ---- Apply knob changes ----
log "--- Applying Knobs ---"
CHANGES=0
for name in "${!KNOBS[@]}"; do
    new_val="${KNOBS[$name]}"
    old_val=$(grep -oP "^\s*${name}\s*=\s*\K[0-9]+" "$CONFIG_FILE" 2>/dev/null)
    if [ -n "$old_val" ] && [ "$old_val" != "$new_val" ]; then
        sed -i "s/^\(\s*${name}\s*=\s*\)[0-9]*/\1${new_val}/" "$CONFIG_FILE"
        log "  $name: $old_val -> $new_val"
        CHANGES=$((CHANGES + 1))
    fi
done
log "Changes applied: $CHANGES"

# ---- Save report ----
{
    rpt "=== E4 Introspection Report === $(date)"
    rpt "Changes: $CHANGES | H=$HEAVY_T M=$MED_T L=$LIGHT_T max=$MAX_T"
    for name in $(echo "${!KNOBS[@]}" | tr ' ' '\n' | sort); do
        printf "  %-30s = %3d\n" "$name" "${KNOBS[$name]}" >> "$REPORT"
    done
    rpt ""
}

# ---- Send SIGUSR1 to resume ----
sleep 0.2
kill -USR1 "$APP_PID" 2>/dev/null
log "SIGUSR1 sent to PID $APP_PID"
log "=== Done ==="
