/*
 * backtrace.c
 *
 *  Created on: Apr 3, 2023
 *      Author: yyan7
 */
#include <execinfo.h>
#include <stdio.h>
#include <stdlib.h>

//Recursive would be challenge
#define MAX_BACKTRACE_DEPTH 32
__thread void * backtrace_ip[MAX_BACKTRACE_DEPTH];
__thread char * backtrace_symbol[MAX_BACKTRACE_DEPTH];
__thread int backtrace_depth = 0;

void retrieve_backtrace() {
  backtrace_depth = backtrace (backtrace_ip, MAX_BACKTRACE_DEPTH);
}
