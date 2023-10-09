/*
 * backtrace.c
 *
 *  Created on: Apr 3, 2023
 *      Author: yyan7
 */
#include <execinfo.h>
#include <stdio.h>
#include <stdlib.h>

__thread void * backtrace_ip[16]; //no recursive so far
__thread char * backtrace_symbol[16];
__thread int backtrace_depth = 0;

void retrieve_backtrace() {
  backtrace_depth = backtrace (backtrace_ip, 16);
}
