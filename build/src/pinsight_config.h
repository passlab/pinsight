#ifndef PINSIGHT_CONFIG_H
#define PINSIGHT_CONFIG_H

#define PINSIGHT_VERSION_MAJOR    0
#define PINSIGHT_VERSION_MINOR    1
#define PINSIGHT_VERSION          0.1

// cmakedefine01 MACRO will define MACRO as either 0 or 1
// cmakedefine MACRO 1 will define MACRO as 1 or leave undefined

#define PINSIGHT_OPENMP
/* #undef PINSIGHT_MPI */
#define PINSIGHT_CUDA
/* #undef PINSIGHT_ENERGY */
#define PINSIGHT_BACKTRACE

#endif /* PINSIGHT_CONFIG_H */
