cmake_minimum_required(VERSION 3.10)
project(pinsight)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_VERBOSE_MAKEFILE ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_BUILD_TYPE Debug)

set(PINSIGHT_VERSION_MAJOR 0)
set(PINSIGHT_VERSION_MINOR 1)
set(PINSIGHT_VERSION ${PINSIGHT_VERSION_MAJOR}.${PINSIGHT_VERSION_MINOR})

option(PINSIGHT_OPENMP       "Build with OpenMP support"       TRUE)
option(PINSIGHT_MPI          "Build with MPI support"          TRUE)
option(PINSIGHT_CUDA         "Build with CUDA support"         TRUE)
option(PINSIGHT_HIP          "Build with HIP/ROCm support"     FALSE)
option(PINSIGHT_ENERGY       "Build with Energy tracing"       FALSE)
option(PINSIGHT_BACKTRACE    "Build with Backtrace enabled"    FALSE)
option(PINSIGHT_PYTHON       "Build with Python tracing support" FALSE)

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/src/pinsight_config.h.cmake.in
        "${CMAKE_CURRENT_BINARY_DIR}/src/pinsight_config.h" @ONLY)

set(SOURCE_FILES
        src/pinsight.h
        src/pinsight.c
        src/pinsight_control_thread.h
        src/pinsight_control_thread.c
        src/backtrace.c
        src/backtrace.h
        src/bitset.c
        src/bitset.h
        src/trace_config.c
        src/trace_config_parse.c
        src/trace_config.h
        src/trace_domain_dsl.h
        src/trace_domain_loader.h
        src/trace_domain_loader.c
        src/enter_exit.c
        src/enter_exit_lttng_ust_tracepoint.h
        src/common_tp_fields_global_lttng_ust_tracepoint.h
        src/app_knob.h
        src/app_knob.c
        )

## For OpenMP, PInsight requires OMPT support in an OpenMP runtime. But buidling PInsight only needs omp.h and ompt.h,
## i.e. no need libomp.so, or -fopenmp flag when compiling or linking PInsight.
if (PINSIGHT_OPENMP)
#    find_package(OpenMP REQUIRED)
#    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${OpenMP_CXX_FLAGS}")
    # if not hardcode here, should be set in cmake as -DOMPLIB_INSTALL=/home/yanyh/tools/llvm-openmp-install
    # only omp.h and ompt.h are needed for building
    set(OPENMP_INCLUDE_PATH "src" CACHE PATH
            "Path to an include folder that contains omp.h and omp-tools.h headers for OpenMP support. By default, the two headers files are provided.")

    include_directories(${OPENMP_INCLUDE_PATH})
    list(APPEND SOURCE_FILES
            src/ompt_lttng_ust_tracepoint.h
            src/ompt_callback.c
            src/trace_domain_OpenMP.h
            )
endif()

## MPI implementation supporting PMPI
if (PINSIGHT_MPI)
    find_package(MPI REQUIRED)
    include_directories(${MPI_C_INCLUDE_PATH} ${MPI_CXX_INCLUDE_PATH})
    #list(APPEND PINSIGHT_EXTERNAL_LIBS ${MPI_C_LIBRARIES} ${MPI_CXX_LIBRARIES})
    list(APPEND SOURCE_FILES
            src/pmpi_lttng_ust_tracepoint.h
            src/pmpi_mpi.c
            src/trace_domain_MPI.h
            )
endif()

## CUDA support, which requires cupti
if (PINSIGHT_CUDA)
    find_package(CUDAToolkit REQUIRED)
    
    include_directories(${CUDAToolkit_INCLUDE_DIRS})
    list(APPEND SOURCE_FILES
            src/cupti_lttng_ust_tracepoint.h
            src/cupti_callback.c
            src/trace_domain_CUDA.h
            )
endif()

## HIP/ROCm support via ROCTracer
if (PINSIGHT_HIP)
    # ROCM_PATH defaults to /opt/rocm if not set in the environment
    if (NOT DEFINED ROCM_PATH)
        set(ROCM_PATH "$ENV{ROCM_PATH}")
        if (NOT ROCM_PATH)
            set(ROCM_PATH "/opt/rocm")
        endif()
    endif()

    find_package(hip REQUIRED HINTS ${ROCM_PATH})

    find_library(ROCTRACER_LIB
        NAMES roctracer64 roctracer
        HINTS ${ROCM_PATH}/lib ${ROCM_PATH}/lib64
        REQUIRED)

    include_directories(${ROCM_PATH}/include)
    list(APPEND SOURCE_FILES
        src/roctracer_lttng_ust_tracepoint.h
        src/roctracer_callback.c
        src/trace_domain_HIP.h
        )
endif()

## Backtrace support, which use GNU extension
if (PINSIGHT_BACKTRACE)
    list(APPEND SOURCE_FILES
            src/backtrace.h
            src/backtrace.c
            )
endif()

## Energy tracing
if (PINSIGHT_ENERGY)
    list(APPEND SOURCE_FILES src/rapl.c src/rapl.h)
endif()


add_library(pinsight SHARED ${SOURCE_FILES})
include_directories(${CMAKE_CURRENT_BINARY_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(pinsight m dl lttng-ust pthread)

if (PINSIGHT_OPENMP)
    target_link_libraries(pinsight omp) # no need libomp.so for pinsight
endif()

if (PINSIGHT_MPI)
	target_link_libraries(pinsight mpi)
endif()

if (PINSIGHT_CUDA)
    target_link_libraries(pinsight CUDA::cudart CUDA::cupti)
endif()

if (PINSIGHT_HIP)
    target_link_libraries(pinsight hip::host ${ROCTRACER_LIB})
    target_compile_definitions(pinsight PRIVATE PINSIGHT_HIP)
endif()

## Python tracing via sys.monitoring (PEP 669, Python 3.12+)
## Must come after add_library(pinsight) since _pinsight_python links against it.
if (PINSIGHT_PYTHON)
    find_package(Python3 COMPONENTS Interpreter Development REQUIRED)
    Python3_add_library(_pinsight_python MODULE src/pysysmon_callback.c)
    target_include_directories(_pinsight_python PRIVATE
        ${CMAKE_CURRENT_BINARY_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(_pinsight_python PRIVATE pinsight lttng-ust)

    # Copy the Python launcher to the build directory for easy testing
    configure_file(${CMAKE_CURRENT_SOURCE_DIR}/src/pinsight.py
                   ${CMAKE_CURRENT_BINARY_DIR}/pinsight/__init__.py COPYONLY)
    configure_file(${CMAKE_CURRENT_SOURCE_DIR}/src/pinsight.py
                   ${CMAKE_CURRENT_BINARY_DIR}/pinsight/__main__.py COPYONLY)
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
    message("    CUDA_TOOLKIT_ROOT_DIR: ${CUDAToolkit_BIN_DIR}/..")
else()
    message("Build with CUDA Support:       FALSE")
endif()
if (PINSIGHT_HIP)
    message("Build with HIP/ROCm Support:   TRUE")
    message("    ROCM_PATH: ${ROCM_PATH}")
    message("    ROCTRACER_LIB: ${ROCTRACER_LIB}")
else()
    message("Build with HIP/ROCm Support:   FALSE")
endif()
if (PINSIGHT_BACKTRACE)
    message("Build with GNU BACKTRACE SUPPORT:  TRUE")
else()
    message("Build with GNU BACKTRACE SUPPORT:  FALSE")
endif()
if (PINSIGHT_PYTHON)
    message("Build with Python Support:     TRUE (Python ${Python3_VERSION})")
    message("    Python3_EXECUTABLE: ${Python3_EXECUTABLE}")
else()
    message("Build with Python Support:     FALSE")
endif()
message("============================================================")
add_executable(test_config_parser 
    test/trace_config_parse/test_config_parser.c
    src/trace_config_parse.c
    src/trace_config.c
    src/bitset.c
    src/trace_domain_loader.c
    src/app_knob.c
)
target_include_directories(test_config_parser PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_BINARY_DIR}/src)
target_link_libraries(test_config_parser m ${CMAKE_DL_LIBS})
