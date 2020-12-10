// Task dependencies are part of OpenMP 4.0, which is only supported in GCC 4.9 or newer.
#include <stdio.h>
#include "omp.h"
#include "callback.h"
int main()
{
  int x = 1;
  int y = 2;
  int z = 3;
#pragma omp parallel
#pragma omp single
  {
//task 1
#pragma omp task shared(x) depend(out: x, y, z)
    x = 2;
    
//task 2
#pragma omp task shared(x) depend(in: x)
    printf("x = %d\n", x);
  
//task 3
#pragma omp task shared(y, z) depend(in: y, z)
    printf("y:z = %d:%d\n", y,z);
  }
  
  return 0;
}
