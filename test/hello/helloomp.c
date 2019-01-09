#include <stdio.h>
#include <omp.h>
#include <unistd.h>

int main(int argc, char* argv[])
 {
   #pragma omp parallel  
   {
	int id = omp_get_thread_num();
   	printf("Hello, world %d.\n", id);
   }
   #pragma omp parallel  
   {
	int id = omp_get_thread_num();
   	printf("Hello, world %d.\n", id);
   }
   return 0;
 }

