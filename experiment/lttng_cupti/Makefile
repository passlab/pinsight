CUDA_INSTALL=/usr/local/cuda
CUPTI_INSTALL=${CUDA_INSTALL}/extras/CUPTI

CFLAGS+=-DPINSIGHT_MPI -DPINSIGHT_OPENMP -g --save-temps
INCLUDES=-I${CUPTI_INSTALL}/include -I${CUDA_INSTALL}/include -I.

LIBS= -L${CUDA_INSTALL}/lib64 -L${CUPTI_INSTALL}/lib64 -lcuda -lcudart -lcupti -ldl -llttng-ust -lstdc++

all: vecadd

libpinsight_cuda.so: cupti_callback.c lttng_cupti_tracepoint.h
	$(CC) $(CFLAGS) ${INCLUDES} -shared -fPIC -o $@ $< $(LIBS)

vecadd: vecadd.o libpinsight_cuda.so
	gcc -o $@ vecadd.o $(LIBS) -L. -lpinsight_cuda

vecadd.o: vecadd.cu
	nvcc -c $(INCLUDES) ${CFLAGS} $<

TRACE_FOLDER=vecadd_cuda_lttng_traces

test: vecadd
	lttng create ompt-tracing-session --output="${TRACE_FOLDER}"
	# Create and enable event rules.
	lttng enable-event --userspace lttng_pinsight_cuda:'*'
	# Start LTTng tracing.
	lttng start
	# Run instrumented code.
	LD_LIBRARY_PATH=.:${CUDA_INSTALL}/lib64:${CUPTI_INSTALL}/lib64:${LD_LIBRARY_PATH} ./$<
	# Stop LTTng tracing.
	lttng stop
	lttng destroy
	# dump traces to text format using babeltrace
	babeltrace ${TRACE_FOLDER} > ${TRACE_FOLDER}.txt

clean:
	rm -rf *.o *.so vecadd

allclean: clean
	rm -rf ${TRACE_FOLDER} ${TRACE_FOLDER}.txt
