cmake_minimum_required(VERSION 3.2)
project(pinsight)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_VERBOSE_MAKEFILE ON)

set(CMAKE_BUILD_TYPE Debug)

set(PINSIGHT_VERSION_MAJOR 0)
set(PINSIGHT_VERSION_MINOR 1)
set(PINSIGHT_VERSION ${PINSIGHT_VERSION_MAJOR}.${PINSIGHT_VERSION_MINOR})

option(PINSIGHT_OPENMP "Build with OpenMP Support"       TRUE)
option(PINSIGHT_MPI    "Build with MPI Support"          TRUE)
option(PINSIGHT_CUDA   "Build with CUDA support"         TRUE)
option(PINSIGHT_ENERGY "Build with Energy tracing"       FALSE)

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/src/pinsight_config.h.cmake.in "${CMAKE_CURRENT_BINARY_DIR}/src/pinsight_config.h" @ONLY)

set(SOURCE_FILES
        src/env_config.c
        src/env_config.h
        src/pinsight.h
        src/pinsight.c
        )

## OpenMP runtime supporting OMPT
if (PINSIGHT_OPENMP)
# We only need ompt.h and omp.h, i.e. no need libomp.so. thus we do not need -fopenmp flag when compiling or linking. 
#    find_package(OpenMP REQUIRED)
#    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${OpenMP_CXX_FLAGS}")
    # if not hardcode here, should be set in cmake as -DOMPLIB_INSTALL=/home/yanyh/tools/llvm-openmp-install
    #set(OMPLIB_INSTALL /home/yanyh/tools/llvm-openmp-install) #
    #link_directories(${OMPLIB_INSTALL}/lib)
    include_directories(${OMPLIB_INSTALL}/include)
    list(APPEND SOURCE_FILES
            src/ompt_lttng_tracepoint.h
            src/ompt_callback.c
            )
endif()

## MPI implementation supporting PMPI
if (PINSIGHT_MPI)
    find_package(MPI REQUIRED)
    include_directories(${MPI_C_INCLUDE_PATH} ${MPI_CXX_INCLUDE_PATH})
    list(APPEND PINSIGHT_EXTERNAL_LIBS ${MPI_C_LIBRARIES} ${MPI_CXX_LIBRARIES})
    list(APPEND SOURCE_FILES
            src/pmpi_lttng_tracepoint.h
            src/pmpi_mpi.c
            )
endif()

## CUDA support, which requires cupti
if (PINSIGHT_CUDA)
endif()

## Energy tracing
if (PINSIGHT_ENERGY)
    list(APPEND SOURCE_FILES src/rapl.c src/rapl.h)
endif()

add_library(pinsight SHARED ${SOURCE_FILES})
include_directories(${CMAKE_CURRENT_BINARY_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(pinsight m dl lttng-ust)
if (PINSIGHT_OPENMP)
#	target_link_libraries(pinsight omp) # no need libomp.so for pinsight
endif()
if (PINSIGHT_MPI)
	target_link_libraries(pinsight mpi)
endif()

install(FILES
        ${CMAKE_CURRENT_BINARY_DIR}/src/pinsight_config.h
        DESTINATION include)

install(TARGETS pinsight
        LIBRARY DESTINATION lib
        )
