// Task dependencies are part of OpenMP 4.0, which is only supported in GCC 4.9 or newer.
#include <stdio.h>
#include "omp.h"
#include "callback.h"


void main(void) {
   int i, j;
#pragma omp taskgroup
   {

#pragma omp taskloop private(j) grainsize(500) nogroup 
      for (i = 0; i < 10000; i++) { // can execute concurrently           
         for (j = 0; j < i; j++) {
            //loop_body(i, j);
            i++; j++;
         }
      }
   }
}
