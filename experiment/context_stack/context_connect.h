/*
 * context_connect.h — experiment for the S2 context-stack design (plan rev 14,
 * pinsight-eval/docs/design/s2_cct_impl_plan.md §3).
 *
 * Core idea under test: keep the last call stack per thread in TLS as
 * (CFA, IP) pairs; at each record-collection point, unwind the live stack
 * step-wise (_Unwind_Backtrace) ONLY until it connects with the cached
 * stack; the record carries a delta (popped count + entered frames) instead
 * of the full 256 B backtrace. Full paths exist only on resync records and
 * in offline replay.
 */
#ifndef CONTEXT_CONNECT_H
#define CONTEXT_CONNECT_H

#include <stdint.h>

#define CTX_MAX_FRAMES 512

typedef struct {
  uint64_t cfa; /* Canonical Frame Address — identifies the ACTIVATION */
  uint64_t ip;  /* return address — identifies the call site */
} ctx_frame_t;

/* What a trace record would carry (the LTTNG_UST_TP_FIELDS_BACKTRACE
 * replacement). entered[] is IPs only, root->leaf order. */
typedef struct {
  uint16_t seq;       /* per-thread record counter, truncated (gap detect) */
  uint8_t resync;     /* 1 = entered[] is the FULL stack; popped = all */
  uint8_t truncated;  /* live stack deeper than CTX_MAX_FRAMES */
  uint16_t popped;    /* frames of the previous recorded path that returned */
  uint16_t n_entered; /* new frames since the previous record */
  uint64_t entered[CTX_MAX_FRAMES];
} ctx_delta_t;

/* Per-thread stats (self-honesty surface of the experiment). */
typedef struct {
  unsigned long records;
  unsigned long resyncs;         /* periodic + forced (no-connect) */
  unsigned long forced_resyncs;  /* walk reached end of stack unconnected */
  unsigned long unwind_steps;    /* total callback invocations (after skip) */
  unsigned long skipped_frames;  /* tool frames skipped */
  unsigned long popped_total;
  unsigned long entered_total;
} ctx_stats_t;

/* resync_period N: every Nth record carries the full stack (N=1 degenerates
 * to full-copy — the old PINSIGHT_BACKTRACE behavior). */
void ctx_thread_init(unsigned resync_period);

/* The record-collection call: connect live stack with the cached one,
 * update the cache (cut + append), fill the delta. */
void ctx_connect(ctx_delta_t *out);

/* Ground-truth helper: full step-wise walk with the same tool-frame skip,
 * NO cache read/update. Returns depth. buf[0] = root-most. */
int ctx_full_walk(ctx_frame_t *buf, int max);

/* Register a function whose frames the walk must skip (the stand-ins for
 * libpinsight's own frames). Call before first use; max 8. */
void ctx_register_tool_fn(void *fn_addr);

int ctx_cached_depth(void);
const ctx_frame_t *ctx_cached_stack(void);
const ctx_stats_t *ctx_stats(void);

#endif
