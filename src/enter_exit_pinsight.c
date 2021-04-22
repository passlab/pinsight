//
// Created by Yonghong Yan on 3/3/20.
//

#include <sys/types.h>
#include <unistd.h>
#include <pinsight.h>

#define TRACEPOINT_CREATE_PROBES
#define TRACEPOINT_DEFINE
#include "enter_exit_pinsight_tracepoint.h"

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
    tracepoint(lttng_pinsight_enter_exit, enter_pinsight);
}

void exit_pinsight_func() {
    //printf("exiting pinsight at host: %s by process: %d\n", hostname, pid);
    tracepoint(lttng_pinsight_enter_exit, exit_pinsight);
}
