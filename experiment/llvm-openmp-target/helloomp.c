#include <stdio.h>
#include <omp.h>
#include <unistd.h>
#include <stdint.h>
#include <inttypes.h>
#include "callback.h"

int main(int argc, char* argv[])
 {
   #pragma omp parallel  
   {
	int id = omp_get_thread_num();
   	printf("Hello, world %d.\n", id);
   }
   printf("Hello, world %" PRIx64 "\n", (uint64_t)(2<<32-1));
   sleep(1);
   printf("===========================================================\n");

   #pragma omp parallel
   {
	int id = omp_get_thread_num();
	int i;
	#pragma omp for 
	for (i=0; i< 100; i++) {
		id += i;
	}
   	printf("Hello, world %d.\n", id);
   }

   return 0;
 }

