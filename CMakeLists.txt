cmake_minimum_required(VERSION 3.5)
project(pinsight)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_VERBOSE_MAKEFILE ON)

set(CMAKE_BUILD_TYPE Debug)

set(PINSIGHT_VERSION_MAJOR 0)
set(PINSIGHT_VERSION_MINOR 1)
set(PINSIGHT_VERSION ${PINSIGHT_VERSION_MAJOR}.${PINSIGHT_VERSION_MINOR})

option(PINSIGHT_OPENMP "Build with OpenMP support"       TRUE)
option(PINSIGHT_MPI    "Build with MPI support"          FALSE)
option(PINSIGHT_CUDA   "Build with CUDA support"         FALSE)
option(PINSIGHT_ENERGY "Build with Energy tracing"       FALSE)

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/src/pinsight_config.h.cmake.in
        "${CMAKE_CURRENT_BINARY_DIR}/src/pinsight_config.h" @ONLY)

set(SOURCE_FILES
        src/lexgion_trace_config.c
        src/lexgion_trace_config.h
        src/pinsight.h
        src/pinsight.c
        src/enter_exit_pinsight.c
        src/enter_exit_pinsight_tracepoint.h
        )

## For OpenMP, PInsight requires OMPT support in an OpenMP runtime. But buidling PInsight only needs omp.h and ompt.h,
## i.e. no need libomp.so, or -fopenmp flag when compiling or linking PInsight.
if (PINSIGHT_OPENMP)
#    find_package(OpenMP REQUIRED)
#    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${OpenMP_CXX_FLAGS}")
    # if not hardcode here, should be set in cmake as -DOMPLIB_INSTALL=/home/yanyh/tools/llvm-openmp-install
    # only omp.h and ompt.h are needed for building
    set(OPENMP_INCLUDE_PATH "/home/yyan7/tools/llvm-openmp-install/include" CACHE PATH
            "Path to an include folder that contains omp.h and ompt.h headers for OpenMP support")

    include_directories(${OPENMP_INCLUDE_PATH})
    list(APPEND SOURCE_FILES
            src/ompt_lttng_tracepoint.h
            src/ompt_callback.c
            )
endif()

## MPI implementation supporting PMPI
if (PINSIGHT_MPI)
    find_package(MPI REQUIRED)
    include_directories(${MPI_C_INCLUDE_PATH} ${MPI_CXX_INCLUDE_PATH})
    #list(APPEND PINSIGHT_EXTERNAL_LIBS ${MPI_C_LIBRARIES} ${MPI_CXX_LIBRARIES})
    list(APPEND SOURCE_FILES
            src/pmpi_lttng_tracepoint.h
            src/pmpi_mpi.c
            )
endif()

## CUDA support, which requires cupti
if (PINSIGHT_CUDA)
    # CUPTI install location needs to be provided, which could be part of CUDA SDK install or a standaline install
    set(CUDA_INSTALL "/usr/local/cuda" CACHE PATH "CUDA SDK install path that includes CUPTI under extras/CUPTI folder" )
    set(CUPTI_INSTALL ${CUDA_INSTALL}/extras/CUPTI)

    include_directories(${CUPTI_INSTALL}/include ${CUDA_INSTALL}/include)
    #list(APPEND PINSIGHT_EXTERNAL_LIBS cupti)

    list(APPEND SOURCE_FILES
            src/cupti_lttng_tracepoint.h
            src/cupti_callback.c
            )
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

if (PINSIGHT_CUDA)
    target_link_libraries(pinsight ${CUPTI_INSTALL}/lib64/libcupti.so)
endif()

install(FILES
        ${CMAKE_CURRENT_BINARY_DIR}/src/pinsight_config.h
        DESTINATION include)

install(TARGETS pinsight
        LIBRARY DESTINATION lib
        )

message("============================================================")
if (PINSIGHT_OPENMP)
    message("Build with OpenMP Support:     TRUE")
    message("    OPENMP_INCLUDE_PATH: ${OPENMP_INCLUDE_PATH}")
else()
    message("Build with OpenMP Support:     FALSE")
endif()

if (PINSIGHT_MPI)
    message("Build with MPI Support:        TRUE")
#    message("    MPI_C_INCLUDE_PATH: ${MPI_C_INCLUDE_PATH}")
#    message("    MPI_CXX_INCLUDE_PATH: ${MPI_CXX_INCLUDE_PATH}")
else()
    message("Build with MPI Support:        FALSE")
endif()

if (PINSIGHT_CUDA)
    message("Build with CUDA Support:       TRUE")
    message("    CUDA_INSTALL: ${CUDA_INSTALL}")
    message("    CUPTI_INSTALL: ${CUPTI_INSTALL}")
else()
    message("Build with CUDA Support:       FALSE")
endif()
message("============================================================")
