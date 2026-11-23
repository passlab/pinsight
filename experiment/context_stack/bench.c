/*
 * bench.c — cost per record: full glibc backtrace() (today's PINSIGHT_BACKTRACE)
 * vs full step-wise _Unwind walk vs the connect (early stop at the cached
 * stack), at several stack depths, steady-state and churn patterns.
 */
#define _GNU_SOURCE
#include "context_connect.h"

#include <execinfo.h>
#include <stdio.h>
#include <time.h>

static volatile int sink;
#define REPS 200000

static unsigned long long now_ns(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (unsigned long long)ts.tv_sec * 1000000000ull + ts.tv_nsec;
}

/* bodies, each registered as a tool frame so the connect's steady state is
 * clean (their frames would otherwise churn per record) */
__attribute__((noinline)) void body_backtrace(void) {
  void *buf[CTX_MAX_FRAMES];
  sink += backtrace(buf, CTX_MAX_FRAMES);
}
__attribute__((noinline)) void body_fullwalk(void) {
  ctx_frame_t buf[CTX_MAX_FRAMES];
  sink += ctx_full_walk(buf, CTX_MAX_FRAMES);
}
__attribute__((noinline)) void body_connect(void) {
  ctx_delta_t d;
  ctx_connect(&d);
  sink += d.n_entered;
}
/* churn: two distinct leaves between records -> 1-2 frames change each time */
__attribute__((noinline)) void churn_a(void) { body_connect(); }
__attribute__((noinline)) void churn_b(void) { body_connect(); }
__attribute__((noinline)) void body_churn(void) {
  churn_a();
  churn_b();
}

typedef void (*body_t)(void);

__attribute__((noinline)) static double run_at_depth(int depth, body_t body,
                                                     int records_per_call) {
  if (depth > 0)
    return run_at_depth(depth - 1, body, records_per_call);
  unsigned long long t0 = now_ns();
  for (int i = 0; i < REPS; i++)
    body();
  unsigned long long t1 = now_ns();
  return (double)(t1 - t0) / ((double)REPS * records_per_call);
}

int main(void) {
  /* tool frames: the connect machinery + harness bodies. churn_a/churn_b are
   * deliberately NOT tools — they are the app-level frames that churn. */
  ctx_register_tool_fn((void *)ctx_connect);
  ctx_register_tool_fn((void *)body_connect);
  ctx_register_tool_fn((void *)ctx_full_walk);

  int depths[] = {8, 32, 128};
  printf("%-28s %10s %10s %10s\n", "ns/record", "depth 8", "depth 32",
         "depth 128");
  struct {
    const char *name;
    body_t body;
    int per_call;
    int reinit;
  } rows[] = {
      {"glibc backtrace (full copy)", body_backtrace, 1, 0},
      {"full _Unwind walk", body_fullwalk, 1, 0},
      {"connect, steady state", body_connect, 1, 1},
      {"connect, 2-frame churn", body_churn, 2, 1},
  };
  for (unsigned r = 0; r < sizeof rows / sizeof rows[0]; r++) {
    printf("%-28s", rows[r].name);
    for (unsigned di = 0; di < 3; di++) {
      if (rows[r].reinit)
        ctx_thread_init(1u << 30); /* no periodic resync: measure pure delta */
      double ns = run_at_depth(depths[di], rows[r].body, rows[r].per_call);
      printf(" %9.0f ", ns);
    }
    printf("\n");
  }
  const ctx_stats_t *st = ctx_stats();
  printf("\n[connect stats, last run] records=%lu unwind_steps/record=%.2f "
         "popped/record=%.3f entered/record=%.3f\n",
         st->records, (double)st->unwind_steps / st->records,
         (double)st->popped_total / st->records,
         (double)st->entered_total / st->records);
  return 0;
}
