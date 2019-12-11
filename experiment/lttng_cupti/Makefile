CUDA_INSTALL=/usr/local/cuda
CUPTI_INSTALL=${CUDA_INSTALL}/extras/CUPTI

INCLUDES=-I${CUPTI_INSTALL}/include

LIBS= -L${CUDA_INSTALL}/lib64 -lcuda -L${CUPTI_INSTALL}/lib64 -lcupti

callback_event: callback_event.o
	nvcc -o $@ $< $(LIBS)

callback_event.o: callback_event.cu
	nvcc -c $(INCLUDES) $<

run: callback_event
	./$<

clean:
	rm -f callback_event callback_event.o
