cmake_minimum_required(VERSION 3.2)
project(visuomp)

set(CMAKE_CXX_STANDARD 11)

set(CMAKE_VERBOSE_MAKEFILE ON)

# Tell CMake where to find the OMP dependencies.
set(OMP_BASE_PATH /opt/llvm-openmp-install)
set(OMP_LIB_PATH /opt/llvm-openmp-install/lib)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g ")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g ")


# Set include directories and source files.
include_directories(. ${OMP_BASE_PATH}/include)
set(SOURCE_FILES
    callback.h
    lttng_tracepoint.h
    visuomp.c
)

# Add 'visuomp' as a shared library build target.
add_library(visuomp SHARED ${SOURCE_FILES})

# Linker include directories.
#link_directories(${OMP_LIB_PATH} ${PAPI_INSTALL}/lib)

# Link the project against libdl, libm, liblttng-ust, and libomp.
target_link_libraries(visuomp dl m lttng-ust ${OMP_LIB_PATH}/libomp.so)

