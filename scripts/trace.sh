#!/bin/bash

# --------------------------------------------------------
# Shell script prelude

USAGE=$(cat <<EOF
Tracing script for use with PInsight.
Usage:
    trace.sh TRACEFILE_DEST OMP_LIB PINSIGHT_LIB ARGS...

Arguments:
  TRACEFILE_DEST    Where to write the LTTng traces.
  OMP_LIB           OpenMP shared library to use with user application.
  PINSIGHT_LIB      PInsight shared library to use with user application.

Examples:
    trace.sh /tmp/ompt-jacobi \\ 
      /opt/openmp-install/lib/libomp.so \\ 
      /opt/pinsight/lib/libvisuomp.so \\ 
      ./jacobi 2048 2048
EOF
)

# Check to make sure user provided enough args.
# If not, then exit early with usage info.
if [ $# -lt 4 ]; then
    echo "Error: Not enough arguments provided.";
    echo "${USAGE}";
    exit 1;
fi

# Read in positional parameters, then shift array.
# This leaves only the program command line in `$@`.
TRACING_OUTPUT_DEST=$1
OMP_LIB_PATH=$2
PINSIGHT_LIB_PATH=$3
shift 3


# --------------------------------------------------------
# Main script

# Create a userspace session.
lttng create ompt-tracing-session --output="${TRACING_OUTPUT_DEST}"

# Create and enable event rules.
lttng enable-event --userspace lttng_pinsight:'*'

# Start LTTng tracing.
lttng start

# Run instrumented code.
LD_PRELOAD=${OMP_LIB_PATH}:${PINSIGHT_LIB_PATH} "$@"

# Stop LTTng tracing.
lttng stop
lttng destroy

