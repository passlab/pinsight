CFLAGS += -fpic -g -I. -I/usr/include/mpi
LDFLAGS += -lm -ldl -llttng-ust -lmpi

.PHONY: clean test

libpinsight_mpi.so:
	$(CC) $(CFLAGS) -shared -o $@ pmpi_mpi.c $(LDFLAGS)

# The order of -L -l is important since it determins which library to use for linking MPI/PMPI code
jacobi_mpi_4np: jacobi_mpi_4np.c libpinsight_mpi.so
	mpicc -o jacobi_mpi_4np jacobi_mpi_4np.c -L. -lpinsight_mpi -L/usr/lib -lmpi -lm

TRACE_FOLDER=jacobi_mpi_4np_lttng_traces
test: jacobi_mpi_4np
	lttng create ompt-tracing-session --output="${TRACE_FOLDER}"
	# Create and enable event rules.
	lttng enable-event --userspace lttng_pinsight_mpi:'*'
	# Start LTTng tracing.
	lttng start
	# Run instrumented code.
	mpirun -np 4 $<
	# Stop LTTng tracing.
	lttng stop
	lttng destroy
	# dump traces to text format using babeltrace
	babeltrace ${TRACE_FOLDER} > ${TRACE_FOLDER}.txt

clean:
	rm -rf libpinsight_mpi.so jacobi_mpi_4np

allclean: clean
	rm -rf ${TRACE_FOLDER} ${TRACE_FOLDER}.txt
