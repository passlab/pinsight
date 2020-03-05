//
// Created by Yonghong Yan on 3/3/20.
//

#include <sys/types.h>
#include <unistd.h>

#define TRACEPOINT_CREATE_PROBES
#define TRACEPOINT_DEFINE
#include "before_after_main_tracepoint.h"

void before_main_func() __attribute__((constructor));
void after_main_func() __attribute__((destructor));

#pragma startup before_main 100
#pragma exit before_main 100

static unsigned int pid;
static char hostname[256];

void before_main_func() {
    pid = getpid();
    gethostname(hostname, 256);
    printf("entering main at host: %s by process: %d\n", hostname, pid);
    tracepoint(lttng_pinsight_before_after_main, before_main, hostname, pid);
}

void after_main_func() {
    printf("exiting main at host: %s by process: %d\n", hostname, pid);
    tracepoint(lttng_pinsight_before_after_main, after_main, hostname, pid);
}
