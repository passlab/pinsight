#!/bin/bash

# --------------------------------------------------------
# Shell script prelude

USAGE=$(cat <<EOF
Tracing script for use with PInsight.
Usage:
    trace.sh TRACEFILE_DEST PINSIGHT_LIB OMP_LIB_PATH OMP_NUM_THREADS ARGS...

Arguments:
  TRACEFILE_DEST    Where to write the LTTng traces.
  PINSIGHT_LIB      PInsight shared library file name to use with user application.
  OMP_LIB_PATH      OpenMP shared library path to use with user application.
  OMP_NUM_THREADS   set this OpenMP env, i.e. number of OpenMP threads to use

Examples:
    trace.sh /tmp/ompt-jacobi \\ 
      /opt/pinsight/lib/libpinsight.so \\ 
      /opt/openmp-install/lib 8 \\
      ./jacobi 2048 2048
EOF
)

# Check to make sure user provided enough args.
# If not, then exit early with usage info.
if [ $# -lt 5 ]; then
    echo "Error: Not enough arguments provided.";
    echo "${USAGE}";
    exit 1;
fi

# Read in positional parameters, then shift array.
# This leaves only the program command line in `$@`.
TRACING_OUTPUT_DEST=$1
PINSIGHT_LIB=$2
OMP_LIB_PATH=$3
export LD_LIBRARY_PATH=${OMP_LIB_PATH}:$LD_LIBRARY_PATH
export OMP_NUM_THREADS=$4
shift 4 

# --------------------------------------------------------
# Main script

# Create a userspace session.
lttng create ompt-tracing-session --output="${TRACING_OUTPUT_DEST}"

# Create and enable event rules.
lttng enable-event --userspace lttng_pinsight:'*'

# Start LTTng tracing.
lttng start

# Run instrumented code.
LD_PRELOAD=${PINSIGHT_LIB} "$@"

# Stop LTTng tracing.
lttng stop
lttng destroy

