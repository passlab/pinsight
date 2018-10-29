OMP_BASE_PATH = /opt/openmp-install
OMP_LIB_PATH = /opt/openmp-install/lib

CFLAGS += -fpic
CFLAGS += -I$(OMP_BASE_PATH)/include -Isrc/
LDFLAGS += -L$(OMP_LIB_PATH) -lm -ldl -llttng-ust -lomp


.PHONY: clean

libvisuomp.so:
	mkdir -p lib/
	$(CC) $(CFLAGS) -shared -o lib/$@ src/callback.h src/lttng_tracepoint.h src/visuomp.c  $(LDFLAGS)

clean:
	rm -rf lib/
