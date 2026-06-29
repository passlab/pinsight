/* GPU-free PInsight test driver: three distinct OpenMP parallel-region sites
 * (= three lexgions) driven at different rates. Used to validate:
 *   - window_timeout (wall-clock window end), and
 *   - window_end_trigger = first | all (count policy).
 * Prints "iter N" to stderr so the synchronous "Auto-trigger (immediate)" line
 * (printed by the app thread when a count policy fires) can be located by iter. */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <omp.h>

__attribute__((noinline)) static void regionA(void){
  #pragma omp parallel
  { volatile int x = omp_get_thread_num(); (void)x; }
}
__attribute__((noinline)) static void regionB(void){
  #pragma omp parallel
  { volatile int x = omp_get_thread_num(); (void)x; }
}
__attribute__((noinline)) static void regionC(void){
  #pragma omp parallel
  { volatile int x = omp_get_thread_num(); (void)x; }
}

int main(int argc, char **argv){
  int iters    = argc>1 ? atoi(argv[1]) : 30;
  int sleep_ms = argc>2 ? atoi(argv[2]) : 0;
  int c_every  = argc>3 ? atoi(argv[3]) : 10;   /* regionC is the SLOW region */
  for (int i = 0; i < iters; i++){
    regionA();
    regionB();
    if (i % c_every == 0) regionC();
    fprintf(stderr, "iter %d done\n", i);
    if (sleep_ms) usleep(sleep_ms*1000);
  }
  fprintf(stderr, "DONE all %d iters\n", iters);
  return 0;
}
