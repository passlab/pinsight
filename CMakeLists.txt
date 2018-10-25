cmake_minimum_required(VERSION 3.2)

set(CMAKE_VERBOSE_MAKEFILE ON)

# Add CMake project in subdirectory.
# Build results deposited in `lib` directory.
add_subdirectory(src lib)
