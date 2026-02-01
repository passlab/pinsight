//
// Created by Yonghong Yan on 3/3/20.
//

#include <sys/types.h>
#include <unistd.h>
#include <pinsight.h>

#define LTTNG_UST_TRACEPOINT_CREATE_PROBES
#define LTTNG_UST_TRACEPOINT_DEFINE
#include "enter_exit_lttng_ust_tracepoint.h"

void enter_pinsight_func() __attribute__((constructor));
void exit_pinsight_func() __attribute__((destructor));

#pragma startup enter_pinsight_func 100
#pragma exit exit_pinsight_func 100

unsigned int pid;
char hostname[48];

#ifdef PINSIGHT_MPI
#include "MPI_domain.h"
#endif

#ifdef PINSIGHT_OPENMP
#include "OpenMP_domain.h"
#endif

#ifdef PINSIGHT_CUDA
#include "CUDA_domain.h"
extern void LTTNG_CUPTI_Init (int rank);
extern void LTTNG_CUPTI_Fini (int rank);
#endif

/**
 * This has to be the initial/main thread of the main program and I do not see a situation that this is not true
 */
void enter_pinsight_func() {
    pid = getpid();
    gethostname(hostname, 48);
    //printf("entering pinsight at host: %s by process: %d\n", hostname, pid);
    lttng_ust_tracepoint(pinsight_enter_exit_lttng_ust, enter_pinsight);
#ifdef PINSIGHT_OPENMP
    register_OpenMP_trace_domain();
    //OpenMP support is initialized by ompt_start_tool() callback that is implemented in ompt_callback.c, thus we do not need to initialize here.
#endif
#ifdef PINSIGHT_MPI
    register_MPI_trace_domain();
#endif
#ifdef PINSIGHT_CUDA
    //TODO: Also need runtime check
    register_CUDA_trace_domain();
    LTTNG_CUPTI_Init (0); //TODO: rank argument should be MPI rank I think, need to fix here when MPI is supported as well
#endif
#ifdef PINSIGHT_ENERGY
#endif
#ifdef PINSIGHT_BACKTRACE
#endif
}

void exit_pinsight_func() {
    //printf("exiting pinsight at host: %s by process: %d\n", hostname, pid);
    lttng_ust_tracepoint(pinsight_enter_exit_lttng_ust, exit_pinsight);
#ifdef PINSIGHT_CUDA
    //TODO: also need runtime check
    LTTNG_CUPTI_Fini(0);
#endif
}
