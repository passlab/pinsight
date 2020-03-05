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
export LD_LIBRARY_PATH=/home/yanyh/tools/llvm-openmp-install/lib:$LD_LIBRARY_PATH

# Clean the trace folder first
rm -rf ${TRACING_OUTPUT_DEST}

# Create a userspace session.
lttng create ${TRACE_NAME}-tracing-session --output="${TRACING_OUTPUT_DEST}"

#lttng add-context --kernel --type=callstack-user --type=callstack-kernel

# Create and enable event rules.
lttng enable-event --userspace lttng_pinsight_enter_exit:'*'
lttng enable-event --userspace lttng_pinsight_ompt:'*'
lttng enable-event --userspace lttng_pinsight_pmpi:'*'
#lttng enable-event --kernel --syscall open,write,read,close

#lttng add-context --userspace --type=hostname
#lttng add-context --userspace --type=ip
#lttng add-context --userspace --type=vpid
#lttng add-context --userspace --type=procname
#lttng add-context --userspace --type=pthread_id
#lttng add-context --userspace --type=vtid


# Start LTTng tracing.
lttng start

# Run instrumented code.
LD_PRELOAD=${PINSIGHT_LIB} "$@"

# Stop LTTng tracing.
lttng stop
lttng destroy

# change the folder name manually from 64-bit to something meaningful for tracecompass to display
mv ${TRACING_OUTPUT_DEST}/ust/uid/${UID}/64-bit ${TRACING_OUTPUT_DEST}/ust/uid/${UID}/${TRACE_NAME} 

