CUDA_INSTALL=/usr/local/cuda
CUPTI_INSTALL=${CUDA_INSTALL}/extras/CUPTI

INCLUDES=-I${CUPTI_INSTALL}/include

LIBS= -L${CUDA_INSTALL}/lib64 -lcuda -L${CUPTI_INSTALL}/lib64 -lcupti

all: callback_event callback_timestamp sass_source_map

callback_event: callback_event.o
	nvcc -o $@ $< $(LIBS)

callback_event.o: callback_event.cu
	nvcc -c $(INCLUDES) $<

callback_timestamp: callback_timestamp.o
	nvcc -o $@ $< $(LIBS)

callback_timestamp.o: callback_timestamp.cu
	nvcc -c $(INCLUDES) $<

sass_source_map: sass_source_map.o
	nvcc -o $@ $< $(LIBS)

sass_source_map.o: sass_source_map.cu
	nvcc -c $(INCLUDES) $<

run: callback_event
	./$<

clean:
	rm -f *.o callback_event callback_timestamp sass_source_map
