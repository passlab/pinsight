#!/bin/bash

# --------------------------------------------------------
# Shell script prelude

USAGE=$(cat <<EOF
Tracing script for use with PInsight.
Usage:
    trace.sh TRACEFILE_DEST OMP_LIB PINSIGHT_LIB OMP_NUM_THREADS ARGS...

Arguments:
  TRACEFILE_DEST    Where to write the LTTng traces.
  OMP_LIB           Full-path file name for OpenMP library to use with user application.
  PINSIGHT_LIB      Full-path PInsight shared library file name to use with user application.
  OMP_NUM_THREADS   A number for setting OMP_NUM_THREADS env

Examples:
    trace.sh /tmp/ompt-jacobi \\ 
      /opt/openmp-install/lib/libomp.so \\
      /opt/pinsight/lib/libpinsight.so 8 \\ 
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
OMP_LIB=$2
PINSIGHT_LIB=$3
export OMP_NUM_THREADS=$4
shift 4 

# --------------------------------------------------------
# Main script

# Clean the trace folder first
rm -rf ${TRACING_OUTPUT_DEST}

# Create a userspace session.
lttng create ompt-tracing-session --output="${TRACING_OUTPUT_DEST}"
#lttng enable-channel --userspace --blocking-timeout=100 blocking-channel

# Create and enable event rules.
lttng enable-event --userspace lttng_pinsight:'*'
#lttng enable-event --userspace lttng_pinsight:'*' --channel=blocking-channel
#lttng enable-event -u -a --channel=blocking-channel

# Add related context field to all channels
#lttng add-context --userspace --type=tid
#lttng add-context --userspace --type=pthread_id
#lttng add-context --userspace --type=vtid
#lttng add-context --userspace --type=perf:thread:cpu-migrations
#lttng add-context --userspace --type=perf:thread:migrations
lttng add-context --userspace --type=perf:thread:cpu-cycles
lttng add-context --userspace --type=perf:thread:cycles
lttng add-context --userspace --type=perf:thread:instructions

# For enabling callstack analysis
#lttng add-context -u -t vpid -t vtid -t procname

# Start LTTng tracing.
lttng start

# Run instrumented code.
# LD_PRELOAD=${OMP_LIB}:${PINSIGHT_LIB} "$@"

# Enable callstack tracing
# LD_PRELOAD=/usr/lib/x86_64-linux-gnu/liblttng-ust-cyg-profile.so:${OMP_LIB}:${PINSIGHT_LIB} LTTNG_UST_ALLOW_BLOCKING=1 "$@"
LD_PRELOAD=${OMP_LIB}:${PINSIGHT_LIB} LTTNG_UST_ALLOW_BLOCKING=1 "$@"

# Stop LTTng tracing.
lttng stop
lttng destroy

# Simply dump using babletrace and grep of all the traces of each thread to the terminal
#for (( i=0; i < $OMP_NUM_THREADS; i++ ));
#for (( i=0; i < 56; i++ ));
#do
#   echo "=================== Thread $i events ============================================="
   # only dump the first 20 matching records
#   babeltrace ${TRACING_OUTPUT_DEST} | grep -m 20 "global_thread_num = $i,"
#done

