#include <stdio.h>
#include <omp.h>
#include <unistd.h>
#include <stdint.h>
#include <inttypes.h>

int main(int argc, char* argv[])
 {
   #pragma omp parallel  
   {
	int id = omp_get_thread_num();
   	printf("Hello, world %d.\n", id);
   }
   #pragma omp parallel num_threads(8)
   {
	int id = omp_get_thread_num();
   	printf("Hello, world %d.\n", id);
   }
   printf("Hello, world %" PRIx64 "\n", (uint64_t)(2<<32-1));
   return 0;
 }

