include environment.mk

# To enable energy measurement, uncomment the following line
#CFLAGS += -DENABLE_ENERGY
CFLAGS += -fpic -g -finstrument-functions
CFLAGS += -I$(OMP_BASE_PATH)/include -Isrc/
LDFLAGS += -L$(OMP_LIB_PATH) -lm -ldl -llttng-ust -lomp

.PHONY: clean test

libpinsight.so:
	mkdir -p lib/
	$(CC) $(CFLAGS) -c src/env_config.c
	$(CC) $(CFLAGS) -c src/rapl.c
	$(CC) $(CFLAGS) -c src/ompt_callback.c
	$(CC) $(CFLAGS) -c src/pinsight.c 
	$(CC) -shared -o lib/$@ pinsight.o ompt_callback.o env_config.o rapl.o $(LDFLAGS)

test:
	$(MAKE) -C test

clean:
	rm -rf lib/
	$(MAKE) -C test clean
