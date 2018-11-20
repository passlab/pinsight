include environment.mk

CFLAGS += -fpic
CFLAGS += -I$(OMP_BASE_PATH)/include -Isrc/
LDFLAGS += -L$(OMP_LIB_PATH) -lm -ldl -llttng-ust -lomp


.PHONY: clean test

libpinsight.so:
	mkdir -p lib/
	$(CC) $(CFLAGS) -shared -o lib/$@ src/callback.h src/lttng_tracepoint.h src/pinsight.c src/env_config.c src/rapl.c $(LDFLAGS)

test:
	$(MAKE) -C test

clean:
	rm -rf lib/
	$(MAKE) -C test clean
