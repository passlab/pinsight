#!/bin/bash
# TraceCompass External Analyses entry point for gpu_datamovement.py
# (use this file's absolute path as the command in TC; see tc-common.sh)
. "$(dirname "$0")/tc-common.sh"
run_lami_analysis gpu_datamovement "$@"
