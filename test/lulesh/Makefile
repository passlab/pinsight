include ../../environment.mk

#default build suggestion of MPI + OPENMP with gcc on Livermore machines you might have to change the compiler name

SHELL = /bin/sh
.SUFFIXES: .cc .o

LULESH_EXEC = lulesh

MPI_INC = /usr/include/mpi
MPI_LIB = /usr/lib

SERCXX = clang++ -DUSE_MPI=0
#MPICXX = OMPI_CC=clang OMPI_CXX=clang++ mpic++ -DUSE_MPI=1
MPICXX = mpic++ -DUSE_MPI=1
CXX = $(MPICXX)
#CXX = g++

SOURCES2.0 = \
	lulesh.cc \
	lulesh-comm.cc \
	lulesh-viz.cc \
	lulesh-util.cc \
	lulesh-init.cc
OBJECTS2.0 = $(SOURCES2.0:.cc=.o)

#Default build suggestions with OpenMP for g++
CXXFLAGS = -g -O3 -fopenmp -I. -Wall -DUSE_MPI=0
LDFLAGS = -g -O3 -fopenmp

#Below are reasonable default flags for a serial build
#CXXFLAGS = -g -O3 -I. -Wall
#LDFLAGS = -g -O3 

#common places you might find silo on the Livermore machines.
#SILO_INCDIR = /opt/local/include
#SILO_LIBDIR = /opt/local/lib
#SILO_INCDIR = ./silo/4.9/1.8.10.1/include
#SILO_LIBDIR = ./silo/4.9/1.8.10.1/lib

#If you do not have silo and visit you can get them at:
#silo:  https://wci.llnl.gov/codes/silo/downloads.html
#visit: https://wci.llnl.gov/codes/visit/download.html

#below is and example of how to make with silo, hdf5 to get vizulization by default all this is turned off.  All paths are Livermore specific.
CXXFLAGS = -g -DVIZ_MESH -I${SILO_INCDIR} -Wall -Wno-pragmas -fopenmp
CXXFLAGS = -g -I${SILO_INCDIR} -Wall -Wno-pragmas -fopenmp
LDFLAGS = -g -L${SILO_LIBDIR} -fopenmp -Wl,-rpath -Wl,${SILO_LIBDIR} -lsiloh5 -lhdf5
LDFLAGS = -g -L${SILO_LIBDIR} -fopenmp -Wl,-rpath

.cc.o: lulesh.h
	@echo "Building $<"
	$(CXX) -c $(CXXFLAGS) -o $@  $<

all: $(LULESH_EXEC)

lulesh: $(OBJECTS2.0)
	@echo "Linking"
	$(CXX) $(OBJECTS2.0) $(LDFLAGS) -lm -o $@

test: lulesh
	LD_PRELOAD=$(OMP_LIB_PATH)/libomp.so:$(PINSIGHT_LIB_PATH)/libpinsight.so ./lulesh

clean:
	/bin/rm -f *.o *~ $(OBJECTS) $(LULESH_EXEC)
	/bin/rm -rf *.dSYM

tar: clean
	cd .. ; tar cvf lulesh-2.0.tar LULESH-2.0 ; mv lulesh-2.0.tar LULESH-2.0

