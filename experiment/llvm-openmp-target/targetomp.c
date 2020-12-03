#include <stdio.h>
#include <omp.h>
#include <unistd.h>
#include <stdint.h>
#include <inttypes.h>
#include "callback.h"

int main(int argc, char* argv[])
{
  int i;
  int len = 1000;
  int a[len];

  for (i=0; i<len; i++)
    a[i]= i;

#pragma omp target map(a[0:len])
#pragma omp parallel for
  for (i=0;i< len;i++)
    a[i]=a[i]+1;
  printf("target test\n");
  return 0;
}
