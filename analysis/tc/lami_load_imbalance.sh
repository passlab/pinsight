#!/bin/bash
# TraceCompass External Analyses entry point for load_imbalance.py
# (use this file's absolute path as the command in TC; see tc-common.sh)
. "$(dirname "$0")/tc-common.sh"
run_lami_analysis load_imbalance "$@"
