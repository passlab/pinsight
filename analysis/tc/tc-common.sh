#!/bin/bash
# Shared logic for the TraceCompass "External Analyses" wrapper scripts
# (lami_<analysis>.sh in this folder). Not called directly.
#
# Owns everything TC-specific and machine-specific so the per-analysis
# wrappers are two lines:
#   - PATH setup: GUI apps get a minimal PATH; python3 and babeltrace2 must
#     resolve. Common homebrew locations are added automatically; anything
#     else goes in tc-local.conf (see below).
#   - Experiment-name resolution: running a TC external analysis on an
#     EXPERIMENT passes the experiment's NAME as the LAST argument (TC's
#     argument order: progress flag, --begin/--end, extra params, trace path
#     last). Resolved here to the member trace folders by mapping the
#     experiment's shadow tree under <project>/Experiments/<name>/ back to the
#     real CTF traces under <project>/Traces/ (see the Python block below).
#   - exec of the generic LAMI adapter (lami_adapter.py) with the analysis
#     module name supplied by the calling wrapper.
#
# Kept bash-3.2 compatible (macOS default bash: no mapfile etc.).

# ================= CONFIG: the three user-customizable paths ==============
# Leave empty for auto-detection/defaults, or set them here directly.
# PREFERRED: set them in tc-local.conf next to this file instead (gitignored
# plain shell, same three variable names) — that survives git updates and
# keeps machine paths out of the repo. tc-local.conf wins over these lines.
PYTHON3="${PYTHON3:-}"                  # python3 executable
                                        #   (empty: first python3 on PATH)
BABELTRACE2_DIR="${BABELTRACE2_DIR:-}"  # dir containing babeltrace2
                                        #   (empty: homebrew locations probed)
TC_WORKSPACE="${TC_WORKSPACE:-}"        # Eclipse workspace (TC title bar)
                                        #   (empty: ~/eclipse-workspace)
# (the ${VAR:-} form also lets environment variables supply values;
#  precedence: tc-local.conf > these lines/environment > built-in default)
# ==========================================================================

TC_HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
[ -f "$TC_HERE/tc-local.conf" ] && . "$TC_HERE/tc-local.conf"

# PATH: probe common tool locations; BABELTRACE2_DIR (if set) wins by going last.
for _d in /opt/homebrew/bin /usr/local/bin "$HOME/homebrew/bin" "${BABELTRACE2_DIR:-}"; do
  [ -n "$_d" ] && [ -d "$_d" ] && PATH="$_d:$PATH"
done
export PATH

PYTHON3="${PYTHON3:-$(command -v python3)}"
TC_WORKSPACE="${TC_WORKSPACE:-$HOME/eclipse-workspace}"
TC_ADAPTER="$TC_HERE/lami_adapter.py"

# run_lami_analysis <analysis_module> [args from TC...]
run_lami_analysis() {
  local analysis="$1"; shift
  local args=("$@")
  local n=${#args[@]}
  if [ "$n" -gt 0 ]; then
    local last="${args[$((n-1))]}"
    case "$last" in
      --*) : ;;   # handshake invocations (--mi-version/--metadata): pass through
      *)
        if [ ! -d "$last" ]; then
          # Not a path -> assume TC experiment name; resolve member locations
          # via the shadow tree under <project>/Experiments/<name>/ (see
          # resolve_experiment.py). A separate script, NOT a heredoc: bash 3.2
          # (macOS default) cannot parse a heredoc inside <(...) process
          # substitution, and its parse error on stderr would be merged into
          # the analysis output by TC (striking the analysis out). Resolver
          # stderr is discarded for the same reason.
          local resolved_out
          resolved_out=$("$PYTHON3" "$TC_HERE/resolve_experiment.py" \
                           "$TC_WORKSPACE" "$last" 2>/dev/null)
          local resolved=()
          local line
          while IFS= read -r line; do
            [ -n "$line" ] && resolved+=("$line")
          done <<< "$resolved_out"
          if [ "${#resolved[@]}" -gt 0 ]; then
            args=("${args[@]:0:$((n-1))}" "${resolved[@]}")
          fi   # unresolved: pass through; the adapter warns and skips it
        fi
        ;;
    esac
  fi
  exec "$PYTHON3" "$TC_ADAPTER" "$analysis" --lami "${args[@]}"
}
