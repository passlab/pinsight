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

void enter_pinsight_func() {
    pid = getpid();
    gethostname(hostname, 48);
    //printf("entering pinsight at host: %s by process: %d\n", hostname, pid);
    lttng_ust_tracepoint(pinsight_enter_exit_lttng_ust, enter_pinsight);
}

void exit_pinsight_func() {
    //printf("exiting pinsight at host: %s by process: %d\n", hostname, pid);
    lttng_ust_tracepoint(pinsight_enter_exit_lttng_ust, exit_pinsight);
}
