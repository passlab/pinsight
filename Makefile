include environment.mk

# To enable energy measurement, uncomment the following line
#CFLAGS += -DENABLE_ENERGY
CFLAGS += -fpic -g -finstrument-functions -save-temps
CFLAGS += -I$(OMP_BASE_PATH)/include -Isrc/ -DPINSIGHT_OPENMP=TRUE
CFLAGS += -I/usr/include/mpi -DPINSIGHT_MPI=TRUE
LDFLAGS += -L$(OMP_LIB_PATH) -lm -ldl -llttng-ust -lomp -lmpi

.PHONY: clean test

libpinsight.so:
	mkdir -p lib/
	$(CC) $(CFLAGS) -c src/env_config.c
	$(CC) $(CFLAGS) -c src/rapl.c
	$(CC) $(CFLAGS) -c src/ompt_callback.c
	$(CC) $(CFLAGS) -c src/pmpi_mpi.c
	$(CC) $(CFLAGS) -c src/pinsight.c
	$(CC) -shared -o lib/$@ pinsight.o pmpi_mpi.o ompt_callback.o env_config.o rapl.o $(LDFLAGS)

test:
	$(MAKE) -C test

clean:
	rm -rf lib/
	$(MAKE) -C test clean
