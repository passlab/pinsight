#!/bin/bash

# --------------------------------------------------------
# Shell script prelude

USAGE=$(cat <<EOF
Usage for this tracing script for use with PInsight:
    trace.sh TRACEFILE_DEST TRACE_NAME PINSIGHT_LIB PROG_AND_ARGS...

Arguments:
  TRACEFILE_DEST    Where to write the LTTng traces.
  TRACE_NAME	    Give a proper name for the trace to be displayed in tracecompass.  
  PINSIGHT_LIB      Full-path PInsight shared library file name to use with user application.

Examples:
    trace.sh ./traces/jacobi jacobi \\ 
      /opt/pinsight/lib/libpinsight.so \\ 
      ./jacobi 2048 2048
    
    trace.sh ./traces/LULESH LULESH \\ 
      /opt/pinsight/lib/libpinsight.so \\ 
      mpirun -np 8 test/LULESH/build/lulesh2.0 -s 20
EOF
)

# Check to make sure user provided enough args.
# If not, then exit early with usage info.
if [ $# -lt 4 ]; then
    echo "********************************************************************";
    echo "**  Error: Not enough arguments provided for the tracing script.  **";
    echo "********************************************************************";
    echo "";
    echo "${USAGE}";
    exit 1;
fi

# Read in positional parameters, then shift array.
# This leaves only the program command line in `$@`.
TRACING_OUTPUT_DEST=$1
TRACE_NAME=$2
PINSIGHT_LIB=$3
shift 3 

# --------------------------------------------------------
# Main script

# Clean the trace folder first
rm -rf ${TRACING_OUTPUT_DEST}

# Create a userspace session.
lttng create ompt-tracing-session --output="${TRACING_OUTPUT_DEST}"
#lttng enable-channel --userspace --blocking-timeout=100 blocking-channel

# Create and enable event rules.
lttng enable-event --userspace lttng_pinsight_ompt:'*'
lttng enable-event --userspace lttng_pinsight_pmpi:'*'

#lttng enable-event --userspace lttng_pinsight_ompt:'*' --channel=blocking-channel
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
# LD_PRELOAD=/usr/lib/x86_64-linux-gnu/liblttng-ust-cyg-profile.so:${PINSIGHT_LIB} LTTNG_UST_ALLOW_BLOCKING=1 "$@"
LD_PRELOAD=${PINSIGHT_LIB} LTTNG_UST_ALLOW_BLOCKING=1 "$@"

# Stop LTTng tracing.
lttng stop
lttng destroy

# change the folder name manually from 64-bit to something meaningful for tracecompass to display
mv ${TRACING_OUTPUT_DEST}/ust/uid/${UID}/64-bit ${TRACING_OUTPUT_DEST}/ust/uid/${UID}/${TRACE_NAME} 

# Simply dump using babletrace and grep of all the traces of each thread to the terminal
#for (( i=0; i < $OMP_NUM_THREADS; i++ ));
#for (( i=0; i < 56; i++ ));
#do
#   echo "=================== Thread $i events ============================================="
   # only dump the first 20 matching records
#   babeltrace ${TRACING_OUTPUT_DEST} | grep -m 20 "global_thread_num = $i,"
#done

