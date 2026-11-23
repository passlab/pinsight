/*
 * context_connect.c — see context_connect.h. Standalone experiment; written
 * so the core can be lifted into src/backtrace.c mostly unchanged.
 *
 * Unwinder: _Unwind_Backtrace (libgcc — what glibc backtrace() is built on,
 * already linked everywhere). One callback per frame; returning a code other
 * than _URC_NO_REASON terminates the walk — that is what makes "unwind only
 * until it connects" possible with zero new dependencies. _Unwind_GetIP /
 * _Unwind_GetCFA give the (ip, cfa) pair per step.
 *
 * Connect = merge of two CFA-sorted sequences (CFAs are monotonic in depth;
 * stack grows down, so unwinding leaf->root sees INCREASING CFAs, and the
 * cached array root->leaf has DECREASING CFAs). Per live frame, against the
 * cached leaf end:
 *   cached cfa <  live cfa            -> cached frame is gone: pop it
 *   cached cfa == live cfa, ip ==     -> CONNECTED: stop the walk
 *   cached cfa == live cfa, ip !=     -> stack slot reused: pop, keep going
 *   otherwise (live deeper than any)  -> live frame is NEW: collect, step
 * The cut is pure truncation: all removals are above the connect point.
 *
 * Tool-frame skip: the walk's top frames are the tool's own (here:
 * ctx_connect / the test's record_event; in pinsight: libpinsight frames).
 * Cached, they would churn on every record and destroy the steady state.
 * We skip while dladdr(ip).dli_saddr matches a registered tool function
 * (production would use libpinsight's address range instead).
 */
#define _GNU_SOURCE
#include "context_connect.h"

#include <dlfcn.h>
#include <stdio.h>
#include <string.h>
#include <unwind.h>

static __thread ctx_frame_t cached[CTX_MAX_FRAMES];
static __thread int cached_depth = 0;
static __thread unsigned resync_n = 256;
static __thread unsigned rec_counter = 0;
static __thread uint16_t seq = 0;
static __thread ctx_stats_t stats;

#define MAX_TOOL_FNS 8
static void *tool_fns[MAX_TOOL_FNS];
static int n_tool_fns = 0;

void ctx_register_tool_fn(void *fn_addr) {
  if (n_tool_fns < MAX_TOOL_FNS)
    tool_fns[n_tool_fns++] = fn_addr;
}

/* A frame is a tool frame if its IP resolves into a registered function.
 * Only the walk's top 2-3 frames ever get here with in_skip set, so the
 * dladdr cost is ~2 calls per record. */
static int is_tool_frame(uint64_t ip) {
  Dl_info info;
  if (!dladdr((void *)(uintptr_t)ip, &info) || !info.dli_saddr)
    return 0;
  for (int i = 0; i < n_tool_fns; i++)
    if (info.dli_saddr == tool_fns[i])
      return 1;
  return 0;
}

/* ---------------- full walk (ground truth; no cache) ---------------- */

struct full_arg {
  ctx_frame_t rev[CTX_MAX_FRAMES]; /* leaf->root as walked */
  int n;
  int in_skip;
};

static _Unwind_Reason_Code full_cb(struct _Unwind_Context *uc, void *p) {
  struct full_arg *a = p;
  uint64_t ip = (uint64_t)_Unwind_GetIP(uc);
  uint64_t cfa = (uint64_t)_Unwind_GetCFA(uc);
  if (ip == 0)
    return _URC_NO_REASON;
  if (a->in_skip) {
    if (is_tool_frame(ip))
      return _URC_NO_REASON;
    a->in_skip = 0;
  }
  if (a->n < CTX_MAX_FRAMES) {
    a->rev[a->n].cfa = cfa;
    a->rev[a->n].ip = ip;
    a->n++;
  }
  return _URC_NO_REASON;
}

int ctx_full_walk(ctx_frame_t *buf, int max) {
  struct full_arg a = {.n = 0, .in_skip = 1};
  _Unwind_Backtrace(full_cb, &a);
  int n = a.n < max ? a.n : max;
  for (int i = 0; i < n; i++) /* reverse to root->leaf */
    buf[i] = a.rev[a.n - 1 - i];
  return n;
}

/* ---------------- the connect ---------------- */

/*
 * ABA hazard found by this experiment (see README): a (cfa, ip) match can be
 * a DIFFERENT activation that reoccupied the same stack slot and sits at the
 * same call site — systematic under regular recursion with unrolled call
 * sites — while a frame BELOW it has advanced to a new call site (its
 * resume-IP moved). A single-frame match would stop there and keep the stale
 * IP below. Remedy: CONFIRMATION — a candidate connect must be followed by
 * K-1 further consecutive cached matches before the walk stops. The frame
 * that advanced is by definition a mismatch, and it sits directly below the
 * rebuilt subtree, so small K kills the systematic case. Cost: +(K-1) unwind
 * steps per record. Residual ABA (K-deep aligned rebuilds) is healed by
 * resync within N records, and is a wrong call SITE inside the correct
 * function, never a wrong function at the matched frame.
 */
#ifndef CTX_CONFIRM_K
#define CTX_CONFIRM_K 3
#endif

struct conn_arg {
  int i;        /* cursor into cached[], from the leaf end */
  int connected;
  int popped;
  int in_skip;
  int n_new;
  int cand;     /* candidate connect index into cached[], -1 = none */
  int pending;  /* consecutive matches confirming the candidate */
  ctx_frame_t new_rev[CTX_MAX_FRAMES]; /* entered frames, leaf->root */
};

static void flush_pending(struct conn_arg *a) {
  /* confirmation failed: the pending matched frames are ambiguous — pop
   * their cached entries and re-enter the (identical) live values, so the
   * walk continues below them */
  for (int k = 0; k < a->pending; k++) {
    int idx = a->cand - k;
    if (a->n_new < CTX_MAX_FRAMES)
      a->new_rev[a->n_new++] = cached[idx];
    a->popped++;
  }
  a->i = a->cand - a->pending;
  a->cand = -1;
  a->pending = 0;
}

static _Unwind_Reason_Code conn_cb(struct _Unwind_Context *uc, void *p) {
  struct conn_arg *a = p;
  uint64_t ip = (uint64_t)_Unwind_GetIP(uc);
  uint64_t cfa = (uint64_t)_Unwind_GetCFA(uc);
  if (ip == 0)
    return _URC_NO_REASON;
  if (a->in_skip) {
    if (is_tool_frame(ip)) {
      stats.skipped_frames++;
      return _URC_NO_REASON;
    }
    a->in_skip = 0;
  }
  stats.unwind_steps++;

  if (a->cand >= 0) { /* confirming a candidate connect */
    int j = a->cand - a->pending;
    if (j < 0) { /* ran past the root while confirming: fully matched */
      a->connected = 1;
      return _URC_NORMAL_STOP;
    }
    if (cached[j].cfa == cfa && cached[j].ip == ip) {
      if (++a->pending >= CTX_CONFIRM_K) {
        a->connected = 1; /* keep cached[0..cand] */
        return _URC_NORMAL_STOP;
      }
      return _URC_NO_REASON;
    }
    flush_pending(a); /* fall through: process this frame normally */
  }

  /* cached frames deeper than the live one are gone */
  while (a->i >= 0 && cached[a->i].cfa < cfa) {
    a->i--;
    a->popped++;
  }
  if (a->i >= 0 && cached[a->i].cfa == cfa) {
    if (cached[a->i].ip == ip) {
      a->cand = a->i; /* candidate; confirm with K-1 more frames */
      a->pending = 1;
      if (CTX_CONFIRM_K <= 1 || a->cand == 0) {
        a->connected = 1;
        return _URC_NORMAL_STOP;
      }
      return _URC_NO_REASON;
    }
    a->i--; /* slot reused by a new call */
    a->popped++;
  }
  if (a->n_new < CTX_MAX_FRAMES) {
    a->new_rev[a->n_new].cfa = cfa;
    a->new_rev[a->n_new].ip = ip;
    a->n_new++;
  }
  return _URC_NO_REASON;
}

void ctx_thread_init(unsigned resync_period) {
  cached_depth = 0;
  resync_n = resync_period ? resync_period : 256;
  rec_counter = 0;
  seq = 0;
  memset(&stats, 0, sizeof stats);
}

void ctx_connect(ctx_delta_t *out) {
  rec_counter++;
  int prev_depth = cached_depth;
  int want_resync = (resync_n == 1) || (rec_counter % resync_n == 0) ||
                    cached_depth == 0;

  struct conn_arg a = {.i = cached_depth - 1, .in_skip = 1, .cand = -1};
  if (want_resync)
    a.i = -1; /* match nothing: the whole live stack lands in new_rev */
  _Unwind_Backtrace(conn_cb, &a);

  if (!a.connected && a.cand >= 0)
    flush_pending(&a); /* walk ended mid-confirmation: keep those frames */
  if (!a.connected && a.i >= 0) {
    /* walk ended (end of stack) without connecting: remaining cached
     * frames are stale — forced resync */
    a.popped += a.i + 1;
    a.i = -1;
    stats.forced_resyncs++;
    want_resync = 1;
  }

  int kept = a.i + 1;
  int truncated = 0;
  int n_new = a.n_new;
  if (kept + n_new > CTX_MAX_FRAMES) { /* cap: drop root-most new frames */
    n_new = CTX_MAX_FRAMES - kept;
    truncated = 1;
  }
  for (int k = 0; k < n_new; k++) /* append entered, reversed to root->leaf */
    cached[kept + k] = a.new_rev[a.n_new - 1 - k];
  cached_depth = kept + n_new;

  out->seq = seq++;
  out->resync = want_resync ? 1 : 0;
  out->truncated = (uint8_t)truncated;
  /* on a resync the delta fields are informational (replay REPLACES the
   * stack); report popped = whole previous path for self-description */
  out->popped = (uint16_t)(want_resync ? prev_depth : a.popped);
  out->n_entered = (uint16_t)n_new;
  for (int k = 0; k < n_new; k++)
    out->entered[k] = a.new_rev[a.n_new - 1 - k].ip;

  stats.records++;
  if (want_resync)
    stats.resyncs++;
  stats.popped_total += a.popped;
  stats.entered_total += n_new;
}

int ctx_cached_depth(void) { return cached_depth; }
const ctx_frame_t *ctx_cached_stack(void) { return cached; }
const ctx_stats_t *ctx_stats(void) { return &stats; }
