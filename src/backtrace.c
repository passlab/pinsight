/*
 * backtrace.c
 *
 *  Created on: Apr 3, 2023
 *      Author: yyan7
 */
#include <execinfo.h>
#include <stdio.h>
#include <stdlib.h>
#include "backtrace.h"

//Recursive would be challenge
#define MAX_BACKTRACE_DEPTH 32
__thread void * backtrace_ip[MAX_BACKTRACE_DEPTH];
//__thread char * backtrace_symbol[MAX_BACKTRACE_DEPTH];
__thread char ** backtrace_symbol;
__thread int backtrace_depth = 0;

void retrieve_backtrace() {
  backtrace_depth = backtrace (backtrace_ip, MAX_BACKTRACE_DEPTH);
#ifdef DEBUG_BACKTRACE_SYMBOL
  backtrace_symbol = backtrace_symbols (backtrace_ip, backtrace_depth);
  printf("================= Backtrace symbols, depth: %d =======================\n", backtrace_depth);
  if (backtrace_symbol != NULL) {
	  for (int i = 0; i < backtrace_depth; i++) {
	   printf("------------------------ %d -------------------------------------\n", i);
       printf ("%s\n", backtrace_symbol[i]);
     }
  }
  printf("========================================================================\n");
  free(backtrace_symbol);
#endif
}
