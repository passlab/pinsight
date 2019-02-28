#!/bin/bash

# --------------------------------------------------------
# Shell script prelude

USAGE=$(cat <<EOF
Tracing script for use with PInsight.
Usage:
    trace.sh TRACEFILE_DEST TRACE_NAME OMP_LIB PINSIGHT_LIB OMP_NUM_THREADS ARGS...

Arguments:
  TRACEFILE_DEST    Where to write the LTTng traces.
  TRACE_NAME	    Give a proper name for the trace to be displayed in tracecompass.  
  OMP_LIB           Full-path file name for OpenMP library to use with user application.
  PINSIGHT_LIB      Full-path PInsight shared library file name to use with user application.
  OMP_NUM_THREADS   A number for setting OMP_NUM_THREADS env

Examples:
    trace.sh /tmp/ompt-jacobi jacobi \\ 
      /opt/openmp-install/lib/libomp.so \\
      /opt/pinsight/lib/libpinsight.so 8 \\ 
      ./jacobi 2048 2048
EOF
)

# Check to make sure user provided enough args.
# If not, then exit early with usage info.
if [ $# -lt 6 ]; then
    echo "Error: Not enough arguments provided.";
    echo "${USAGE}";
    exit 1;
fi

# Read in positional parameters, then shift array.
# This leaves only the program command line in `$@`.
TRACING_OUTPUT_DEST=$1
TRACE_NAME=$2
OMP_LIB=$3
PINSIGHT_LIB=$4
export OMP_NUM_THREADS=$5
shift 5 

# --------------------------------------------------------
# Main script

# Create a userspace session.
lttng create ompt-tracing-session --output="${TRACING_OUTPUT_DEST}"

# Create and enable event rules.
lttng enable-event --userspace lttng_pinsight:'*'

# Start LTTng tracing.
lttng start

# Run instrumented code.
LD_PRELOAD=${OMP_LIB}:${PINSIGHT_LIB} "$@"

# Stop LTTng tracing.
lttng stop
lttng destroy

# change the folder name manually from 64-bit to something meaningful for tracecompass to display
mv ${TRACING_OUTPUT_DEST}/ust/uid/${UID}/64-bit ${TRACING_OUTPUT_DEST}/ust/uid/${UID}/${TRACE_NAME} 

