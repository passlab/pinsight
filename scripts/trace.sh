#!/bin/bash

# --------------------------------------------------------
# Shell script prelude

USAGE=$(cat <<EOF
Usage for this tracing script for use with PInsight:
    trace.sh TRACEFILE_DEST TRACE_NAME PINSIGHT_LIB LD_LIBRARY_PATH_PREPEND PROG_AND_ARGS...

Arguments:
  TRACEFILE_DEST    Where to write the LTTng traces.
  TRACE_NAME	    Give a proper name for the trace to be displayed in tracecompass.
  PINSIGHT_LIB      Full-path PInsight shared library file name to use with user application.
  LD_LIBRARY_PATH_PREPEND   A list of paths separated by :. The list is prepended to 
		    the LD_LIBRARY_PATH env. This argument can be used to provide
	            path for the libraries used by the pinsight tracing, such as
		    path for libomp.so or libmpi.so. If none is needed, e.g. the path are
		    already set in the LD_LIBRARY_PATH, : should be provided for this arg.

Examples:
    trace.sh ./traces/jacobi jacobi \\
      /opt/pinsight/lib/libpinsight.so /opt/llvm-install/lib \\
      ./test/jacobi 2048 2048

    trace.sh ./traces/LULESH LULESH \\
      /opt/pinsight/lib/libpinsight.so /opt/llvm-install/lib:/opt/openmpi-install/lib \\
      mpirun -np 8 test/LULESH/build/lulesh2.0 -s 20

    # AMD HIP/ROCm app with energy (lib built with PINSIGHT_HIP and/or
    # PINSIGHT_ENERGY_AMD_GPU): prepend the ROCm lib dir for libroctracer64/libamd_smi.
    trace.sh ./traces/vecadd vecadd \\
      \$HOME/tools/pinsight/build_hip/libpinsight.so /opt/rocm-7.2.1/lib \\
      ./test/rocm/vecadd_pinsight

    # Python app (lib built with PINSIGHT_PYTHON): launch via "python3 -m pinsight"
    # and export PYTHONPATH to the PInsight build dir (pinsight.py + _pinsight_python*.so)
    # before calling this script.
    #   export PYTHONPATH=\$HOME/tools/pinsight/build_omp
    trace.sh ./traces/pyapp pyapp \\
      \$HOME/tools/pinsight/build_omp/libpinsight.so : \\
      python3 -m pinsight ./app.py

Note: energy tracing needs no special launch — energy_enter/energy_exit are
emitted automatically when the library is built with PINSIGHT_ENERGY[_AMD_GPU].
EOF
)

# Check to make sure user provided enough args.
# If not, then exit early with usage info.
if [ $# -lt 5 ]; then
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
LD_LIBRARY_PATH_PREPEND=$4
shift 4 

# setting LD_LIBRARY_PATH with the provided LD_LIBRARY_PATH_PREPEND
export LD_LIBRARY_PATH=${LD_LIBRARY_PATH_PREPEND}:$LD_LIBRARY_PATH

echo LD_LIBRARY_PATH=$LD_LIBRARY_PATH

# Clean the trace folder first (disabled: let caller manage trace dirs)
# rm -rf ${TRACING_OUTPUT_DEST}

# --------------------------------------------------------
# Main tracing commands

# Create a userspace session.
#lttng create ${TRACE_NAME}-tracing-session --snapshot --output="${TRACING_OUTPUT_DEST}"
lttng create ${TRACE_NAME}-tracing-session --output="${TRACING_OUTPUT_DEST}"

# Create and enable event rules — one per PInsight tracepoint provider.
# (The catch-all "enable-event -u -a" below also captures these; this explicit
# list keeps every PInsight domain traced even if that catch-all is disabled.)
lttng enable-event --userspace pinsight_enter_exit_lttng_ust:'*'
lttng enable-event --userspace ompt_pinsight_lttng_ust:'*'
lttng enable-event --userspace pmpi_pinsight_lttng_ust:'*'
lttng enable-event --userspace cupti_pinsight_lttng_ust:'*'
lttng enable-event --userspace roctracer_pinsight_lttng_ust:'*'   # AMD HIP/ROCm
lttng enable-event --userspace pysysmon_pinsight_lttng_ust:'*'    # Python sys.monitoring
lttng enable-event --userspace energy_pinsight_lttng_ust:'*'      # Energy/power

# Experimental kernel trace events
#lttng enable-event --kernel --syscall open,write,read,close
#lttng add-context --userspace --type=hostname
#lttng add-context --userspace --type=ip
#lttng add-context --userspace --type=vpid
#lttng add-context --userspace --type=procname
#lttng add-context --userspace --type=pthread_id
#lttng add-context --userspace --type=vtid
#lttng add-context --userspace --type=tid
#lttng add-context --userspace --type=pthread_id
#lttng add-context --userspace --type=vtid
#lttng add-context --userspace --type=perf:thread:cpu-migrations
#lttng add-context --userspace --type=perf:thread:migrations
#lttng add-context --userspace --type=perf:thread:cpu-cycles
#lttng add-context --userspace --type=perf:thread:cycles
#lttng add-context --userspace --type=perf:thread:instructions


# For enabling source lookup, but only for looking up sources that have the LTTng UST tracepoint call, i.e. the ompt/pmpi/cupti callbacks
#lttng enable-event -u -a
#lttng add-context -u -t vpid -t ip

# For enabling callstack analysis
lttng enable-event -u -a
lttng add-context -u -t procname -t vpid -t vtid -t ip

# Experimental kernel events
#lttng add-context --kernel --type=callstack-user --type=callstack-kernel
# lttng enable-event --kernel --all

# Start LTTng tracing.
lttng start

# Run instrumented code.
# OMP_TOOL_LIBRARIES is the workaround to use the Ubuntu-distributed LLVM OpenMP runtime for OMPT
# https://bugs.launchpad.net/ubuntu/+source/llvm-defaults/+bug/1899199
#LD_PRELOAD=${PINSIGHT_LIB} "$@"
#LD_PRELOAD=/usr/lib/x86_64-linux-gnu/liblttng-ust-cyg-profile.so:/usr/lib/x86_64-linux-gnu/liblttng-ust-dl.so OMP_TOOL_LIBRARIES=${PINSIGHT_LIB} "$@"
#LD_PRELOAD=/usr/lib/x86_64-linux-gnu/liblttng-ust-cyg-profile-fast.so
LD_PRELOAD="${PINSIGHT_LIB} ${LD_PRELOAD}" OMP_TOOL_LIBRARIES=${PINSIGHT_LIB} "$@"

# LD_PRELOAD=/usr/lib/x86_64-linux-gnu/liblttng-ust-cyg-profile.so:${PINSIGHT_LIB} LTTNG_UST_ALLOW_BLOCKING=1 "$@"
# LD_PRELOAD=${PINSIGHT_LIB} LTTNG_UST_ALLOW_BLOCKING=1 "$@"

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

