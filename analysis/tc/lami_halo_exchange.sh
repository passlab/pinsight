#!/bin/bash
# TraceCompass External Analyses entry point for halo_exchange.py
# (use this file's absolute path as the command in TC; see tc-common.sh)
. "$(dirname "$0")/tc-common.sh"
run_lami_analysis halo_exchange "$@"
