include environment.mk

CFLAGS += -fpic
CFLAGS += -I$(OMP_BASE_PATH)/include -Isrc/
LDFLAGS += -L$(OMP_LIB_PATH) -lm -ldl -llttng-ust -lomp


.PHONY: clean test

libvisuomp.so:
	mkdir -p lib/
	$(CC) $(CFLAGS) -shared -o lib/$@ src/callback.h src/lttng_tracepoint.h src/visuomp.c  $(LDFLAGS)

test:
	$(MAKE) -C test

clean:
	rm -rf lib/
	$(MAKE) -C test clean