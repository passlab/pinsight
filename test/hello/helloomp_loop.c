#include <inttypes.h>
#include <omp.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

int main(int argc, char *argv[]) {
  int i;
  for (i = 0; i < 100; i++) {
#pragma omp parallel
    {
    }
  }
  return 0;
}
