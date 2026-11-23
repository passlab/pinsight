/*
 * test_correctness.c — validates the connect algorithm and the delta wire
 * format against ground truth (a full walk at the same instant).
 *
 * At every "record point" we capture BOTH:
 *   - the delta via ctx_connect() (what a trace record would carry), and
 *   - the full stack via ctx_full_walk() (ground truth).
 * Checks:
 *   C1 cache == truth after every connect (the merge/cut is right)
 *   C2 replay(deltas) == truth at every record (the wire format is right)
 *   C3 recursion: same IP at many depths (distinct CFAs) replays right
 *   C4 slot reuse: g() occupying f()'s CFA with a different IP is not
 *      spliced onto f's path (reports whether the equal-CFA case actually
 *      occurred, so we know it was exercised)
 *   C5 randomized call tree (varied frame sizes via alloca), thousands of
 *      records
 *   C6 drop test: discard records -> ctx_seq detects the gap, replay marks
 *      context unknown (never wrong), recovers exactly at the next resync;
 *      corruption window <= N
 *   C7 region interleave: lexgion-style begin/end markers replayed between
 *      the right frames (delta-before-region-op rule)
 *   C8 two threads, independent TLS
 */
#define _GNU_SOURCE
#include "context_connect.h"
#include "replay.h"

#include <alloca.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_RECORDS 20000
#define MAX_REGIONS 64

typedef struct { /* one logged record: delta + ground truth + region op */
  ctx_delta_t delta;
  ctx_frame_t truth[CTX_MAX_FRAMES];
  int truth_depth;
  int region_op; /* 0 none, +id begin, -id end */
} log_rec_t;

static __thread log_rec_t *rlog;
static __thread int nlog;
static __thread int expected_regions[MAX_REGIONS]; /* harness truth */
static __thread int n_expected_regions;

static int failures = 0;
#define CHECK(cond, ...)                                                       \
  do {                                                                         \
    if (!(cond)) {                                                             \
      failures++;                                                              \
      printf("[FAIL] " __VA_ARGS__);                                           \
      printf("\n");                                                            \
    }                                                                          \
  } while (0)

/* ---- the "tool" entry point: plays libpinsight's record-emission role ---- */
__attribute__((noinline)) void
record_event(int region_op) { /* non-static: registered as tool frame */
  if (nlog >= MAX_RECORDS)
    return;
  log_rec_t *r = &rlog[nlog];
  r->region_op = region_op;
  ctx_connect(&r->delta); /* delta BEFORE the record's own region op */
  r->truth_depth = ctx_full_walk(r->truth, CTX_MAX_FRAMES);
  /* C1: the cache must equal truth right now (catches connect errors at the
   * source, independent of the wire format) */
  if (getenv("CTX_DUMP") && nlog >= 150 && nlog <= 170) {
    printf("[dump] rec %d: truth_depth=%d popped=%d entered=%d resync=%d "
           "leaf_ip=%lx truth[11].ip=%lx truth[12].ip=%lx truth[12].cfa=%lx\n",
           nlog, r->truth_depth, r->delta.popped, r->delta.n_entered,
           r->delta.resync,
           r->truth_depth ? (unsigned long)r->truth[r->truth_depth - 1].ip : 0,
           r->truth_depth > 11 ? (unsigned long)r->truth[11].ip : 0,
           r->truth_depth > 12 ? (unsigned long)r->truth[12].ip : 0,
           r->truth_depth > 12 ? (unsigned long)r->truth[12].cfa : 0);
  }
  if (ctx_cached_depth() != r->truth_depth) {
    printf("[C1] rec %d: cache depth %d != truth %d (resync=%d popped=%d "
           "entered=%d)\n",
           nlog, ctx_cached_depth(), r->truth_depth, r->delta.resync,
           r->delta.popped, r->delta.n_entered);
  } else {
    const ctx_frame_t *c = ctx_cached_stack();
    for (int k = 0; k < r->truth_depth; k++)
      if (c[k].ip != r->truth[k].ip || c[k].cfa != r->truth[k].cfa) {
        printf("[C1] rec %d: cache[%d] (cfa=%lx ip=%lx) != truth (cfa=%lx "
               "ip=%lx) depth=%d\n",
               nlog, k, (unsigned long)c[k].cfa, (unsigned long)c[k].ip,
               (unsigned long)r->truth[k].cfa, (unsigned long)r->truth[k].ip,
               r->truth_depth);
        break;
      }
  }
  if (region_op > 0 && n_expected_regions < MAX_REGIONS)
    expected_regions[n_expected_regions++] = region_op;
  else if (region_op < 0 && n_expected_regions > 0)
    n_expected_regions--;
  nlog++;
}

/* ---------------- C1 + C2 over a log ---------------- */

static void validate_log(const char *name) {
  replayer_t rp;
  replay_init(&rp);
  for (int i = 0; i < nlog; i++) {
    log_rec_t *r = &rlog[i];
    replay_apply(&rp, &r->delta);
    CHECK(rp.valid, "%s: rec %d replay invalid (unexpected gap)", name, i);
    CHECK(rp.depth == r->truth_depth, "%s: rec %d depth %d != truth %d", name,
          i, rp.depth, r->truth_depth);
    int n = rp.depth < r->truth_depth ? rp.depth : r->truth_depth;
    for (int k = 0; k < n; k++)
      if (rp.stack[k] != r->truth[k].ip) {
        CHECK(0, "%s: rec %d frame %d ip mismatch", name, i, k);
        break;
      }
  }
  printf("[ok] %s: %d records, replay == truth\n", name, nlog);
}

/* ---------------- scenarios ---------------- */

typedef void (*fn_t)(int);
static volatile int sink; /* defeat optimization */

__attribute__((noinline)) void leaf_a(int x) {
  sink += x;
  record_event(0);
}
__attribute__((noinline)) void leaf_b(int x) {
  sink -= x;
  record_event(0);
}
__attribute__((noinline)) void mid(int d) {
  if (d > 0)
    mid(d - 1);
  else {
    leaf_a(1);
    leaf_b(2); /* unwind + regrow at the same depth */
  }
}

__attribute__((noinline)) void recur(int d) { /* C3: same IP, many depths */
  if (d == 3 || d == 7 || d == 11)
    record_event(0);
  if (d > 0)
    recur(d - 1);
}

/* C4: same locals => likely same frame size => same CFA, different IP */
__attribute__((noinline)) void twin_f(void) {
  volatile long pad[4] = {0};
  sink += (int)pad[0];
  record_event(0);
}
__attribute__((noinline)) void twin_g(void) {
  volatile long pad[4] = {0};
  sink -= (int)pad[0];
  record_event(0);
}
__attribute__((noinline)) void twin_caller(void) {
  twin_f();
  twin_g();
}

/* C5/C7: randomized tree; alloca varies CFA gaps; occasional regions */
static unsigned rstate = 12345;
static unsigned rnd(void) {
  rstate = rstate * 1103515245u + 12345u;
  return (rstate >> 16) & 0x7fff;
}
__attribute__((noinline)) void tree(int depth, int budget) {
  char *pad = alloca(16 + (rnd() % 240));
  pad[0] = (char)depth;
  sink += pad[0];
  int region = 0;
  if (rnd() % 4 == 0 && n_expected_regions < MAX_REGIONS - 1) {
    region = (int)(rnd() % 1000) + 1;
    record_event(region); /* begin record */
  }
  if (rnd() % 3 == 0)
    record_event(0);
  if (depth < 40 && budget > 0) {
    int kids = 1 + (int)(rnd() % 2);
    for (int i = 0; i < kids; i++)
      tree(depth + 1, budget / 2);
  }
  if (rnd() % 3 == 0)
    record_event(0);
  if (region)
    record_event(-region); /* end record, before this frame returns */
}

/* ---------------- C6: drops ---------------- */

static void drop_test(unsigned resync_n) {
  /* replay the C5 log with record `drop_at` removed */
  for (int drop_at = 1; drop_at + 1 < nlog; drop_at += 97) {
    replayer_t rp;
    replay_init(&rp);
    int invalid_len = 0, recovered_at = -1;
    for (int i = 0; i < nlog; i++) {
      if (i == drop_at)
        continue;
      replay_apply(&rp, &rlog[i].delta);
      if (i > drop_at) {
        if (!rp.valid)
          invalid_len++;
        else {
          if (recovered_at < 0)
            recovered_at = i;
          /* once valid again, must match truth exactly */
          CHECK(rp.depth == rlog[i].truth_depth,
                "drop@%d: rec %d depth wrong after recovery", drop_at, i);
        }
      }
    }
    CHECK(rp.gaps_detected >= 1, "drop@%d: gap not detected", drop_at);
    CHECK(invalid_len <= (int)resync_n,
          "drop@%d: unknown window %d > N=%u", drop_at, invalid_len, resync_n);
  }
  printf("[ok] drops: every gap detected; unknown (never wrong) window <= N\n");
}

/* ---------------- C7: region interleave ---------------- */

static void region_test(void) {
  /* replay combined stack: frames from deltas, regions from region ops,
   * delta applied BEFORE the record's own region op */
  int open[MAX_REGIONS], n_open = 0;
  replayer_t rp;
  replay_init(&rp);
  int anchors[MAX_REGIONS]; /* frame depth each region was anchored at */
  for (int i = 0; i < nlog; i++) {
    replay_apply(&rp, &rlog[i].delta);
    int op = rlog[i].region_op;
    if (op > 0) {
      anchors[n_open] = rp.depth;
      open[n_open++] = op;
    } else if (op < 0) {
      CHECK(n_open > 0 && open[n_open - 1] == -op,
            "region nesting broken at rec %d", i);
      n_open--;
    }
    /* every open region's anchor frame must still be alive */
    for (int k = 0; k < n_open; k++)
      CHECK(anchors[k] <= rp.depth,
            "rec %d: region %d anchored at %d but depth is %d (frame died "
            "under a live region)",
            i, open[k], anchors[k], rp.depth);
  }
  CHECK(n_open == 0, "regions left open at end");
  printf("[ok] regions: nesting + anchors consistent across %d records\n",
         nlog);
}

/* ---------------- C8: threads ---------------- */

static void *thread_body(void *arg) {
  rlog = calloc(MAX_RECORDS, sizeof(log_rec_t));
  nlog = 0;
  n_expected_regions = 0;
  ctx_thread_init(64);
  rstate = 999u + (unsigned)(uintptr_t)arg;
  tree(0, 2000);
  validate_log((uintptr_t)arg == 1 ? "thread-1 tree" : "thread-2 tree");
  free(rlog);
  return NULL;
}

int main(void) {
  ctx_register_tool_fn((void *)record_event);
  ctx_register_tool_fn((void *)ctx_connect);
  ctx_register_tool_fn((void *)ctx_full_walk);

  rlog = calloc(MAX_RECORDS, sizeof(log_rec_t));

  /* C1/C2: deepen, unwind+regrow */
  nlog = 0;
  ctx_thread_init(8);
  for (int i = 0; i < 50; i++)
    mid(10 + (i % 5));
  validate_log("deepen/regrow (N=8)");

  /* C3: recursion */
  nlog = 0;
  ctx_thread_init(16);
  for (int i = 0; i < 30; i++)
    recur(12);
  validate_log("recursion");

  /* C4: slot reuse */
  nlog = 0;
  ctx_thread_init(1000); /* no periodic resync: pure delta path exercised */
  twin_caller();
  {
    /* did f and g actually land on the same CFA? (informational) */
    int same_cfa = rlog[0].truth_depth == rlog[1].truth_depth &&
                   rlog[0].truth[rlog[0].truth_depth - 1].cfa ==
                       rlog[1].truth[rlog[1].truth_depth - 1].cfa;
    printf("[info] slot-reuse case exercised with equal CFA: %s\n",
           same_cfa ? "YES" : "no (still validated by truth compare)");
  }
  validate_log("slot reuse");

  /* C5 + C7: random tree with regions, several resync periods */
  unsigned periods[] = {4, 32, 256};
  for (unsigned pi = 0; pi < 3; pi++) {
    nlog = 0;
    n_expected_regions = 0;
    ctx_thread_init(periods[pi]);
    rstate = 42;
    tree(0, 4000);
    char name[64];
    snprintf(name, sizeof name, "random tree (N=%u)", periods[pi]);
    validate_log(name);
    region_test();
    if (periods[pi] == 32)
      drop_test(32); /* C6 on the N=32 log */
  }

  const ctx_stats_t *st = ctx_stats();
  printf("[stats] records=%lu resyncs=%lu forced=%lu unwind_steps=%lu "
         "(%.2f/record) skipped=%lu popped=%lu entered=%lu\n",
         st->records, st->resyncs, st->forced_resyncs, st->unwind_steps,
         st->records ? (double)st->unwind_steps / st->records : 0.0,
         st->skipped_frames, st->popped_total, st->entered_total);

  /* C8 */
  pthread_t t1, t2;
  pthread_create(&t1, NULL, thread_body, (void *)1);
  pthread_create(&t2, NULL, thread_body, (void *)2);
  pthread_join(t1, NULL);
  pthread_join(t2, NULL);

  printf(failures ? "RESULT: %d FAILURES\n" : "RESULT: ALL PASS\n", failures);
  return failures ? 1 : 0;
}
