/*
 * replay.h — the offline side of the experiment: rebuild per-thread full
 * call paths from delta records (what the trace reader will do). Includes
 * ctx_seq gap detection: a lost record marks the context UNKNOWN (not
 * silently wrong) until the next resync record re-baselines it.
 */
#ifndef REPLAY_H
#define REPLAY_H

#include "context_connect.h"

typedef struct {
  uint64_t stack[CTX_MAX_FRAMES]; /* IPs, root->leaf */
  int depth;
  int valid;         /* 0 after a detected gap, until next resync */
  int have_seq;
  uint16_t next_seq; /* expected seq of the next record */
  unsigned long gaps_detected;
} replayer_t;

static inline void replay_init(replayer_t *r) {
  r->depth = 0;
  r->valid = 1;
  r->have_seq = 0;
  r->next_seq = 0;
  r->gaps_detected = 0;
}

static inline void replay_apply(replayer_t *r, const ctx_delta_t *d) {
  if (r->have_seq && d->seq != r->next_seq) {
    r->gaps_detected++;
    r->valid = 0; /* context unknown from here... */
  }
  r->next_seq = (uint16_t)(d->seq + 1);
  r->have_seq = 1;

  if (d->resync) { /* ...until a resync re-baselines it */
    r->depth = 0;
    for (int i = 0; i < d->n_entered && i < CTX_MAX_FRAMES; i++)
      r->stack[r->depth++] = d->entered[i];
    r->valid = 1;
    return;
  }
  if (!r->valid)
    return; /* known-unknown: don't build a wrong stack */
  r->depth -= d->popped;
  if (r->depth < 0)
    r->depth = 0;
  for (int i = 0; i < d->n_entered && r->depth < CTX_MAX_FRAMES; i++)
    r->stack[r->depth++] = d->entered[i];
}

#endif
