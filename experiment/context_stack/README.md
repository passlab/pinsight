# context_stack — experiment for the S2 context-stack design

Prototype + validation for the design in
`pinsight-eval/docs/design/s2_cct_impl_plan.md` §3 (rev 14/15): keep the
last call stack per thread in TLS as (CFA, IP) pairs; at each record,
unwind step-wise (`_Unwind_Backtrace`, libgcc — no new dependency) only
until the live stack connects with the cached one; the record carries a
delta (`ctx_popped` + `ctx_entered[]` + `ctx_seq` + periodic resync)
instead of a full 256 B backtrace; full paths are rebuilt offline by
replay.

Files:
- `context_connect.{c,h}` — the core, written to be lifted into
  `src/backtrace.c`: cached TLS stack, step-wise connect with early stop,
  cut-by-truncation, tool-frame skip, delta + resync + stats.
- `replay.h` — the offline reader logic: delta replay + `ctx_seq` gap
  detection (lost record → context *unknown*, never silently wrong, until
  the next resync).
- `test_correctness.c` — validates cache == truth and replay == truth at
  every record (truth = full walk at the same instant): deepen/regrow,
  recursion, stack-slot reuse (equal-CFA case confirmed exercised),
  randomized call tree with `alloca`-varied frames and lexgion-style
  region markers, drop/resync recovery, two threads.
- `bench.c` — cost per record vs today's full `backtrace()` copy.

`make run` builds and runs both. Status 2026-10-19: **ALL PASS**.

## Finding 1 — the ABA hazard is real and systematic (and fixed)

A plain single-frame (CFA, IP) match is NOT safe. The test's random tree
reproduced it deterministically: a frame's resume-IP legitimately moves
when the same activation calls from a different call site (gcc had
unrolled a loop into two call sites); its old child returned and the NEW
child reoccupied the same CFA at the same internal call site — (CFA, IP)
collide with the dead activation, the walk stopped there, and the stale
parent IP survived below the connect point (wrong call site, correct
function; healed at the next resync — all observed before the fix).

**Fix: confirmation depth K** (`CTX_CONFIRM_K`, default 3): a candidate
connect must be followed by K−1 further consecutive cached matches before
the walk stops. The frame that advanced its call site is by definition a
mismatch and sits directly below the rebuilt subtree, so small K kills the
systematic case. Cost: +(K−1) unwind steps per record. Residual ABA
(K-deep aligned rebuilds) remains theoretically possible; it is bounded by
resync (≤ N records), and is a wrong call site inside the correct
function, never a wrong function at the matched frame. With K=3 all tests
pass including the adversarial tree.

## Finding 2 — cost (this box, gcc -O2, 200k reps)

| ns/record            | depth 8 | depth 32 | depth 128 |
|----------------------|--------:|---------:|----------:|
| glibc backtrace (full copy, today) | 1687 | 4571 | 16538 |
| full `_Unwind` walk  | 1928 | 4705 | 16724 |
| connect, steady state | 976 | 980 | **978** |
| connect, 2-frame churn | 1065 | 1080 | 1101 |

- Connect is **depth-independent** (~1 µs flat); full backtrace is
  O(depth) — 4.7× cheaper at depth 32, 17× at depth 128. Plus the record
  payload drops from 256 B to ~4 B steady state.
- The ~1 µs floor is mostly fixed cost: `_Unwind` context init + the
  `dladdr`-based tool-frame skip (2 calls/record in the experiment).
  Production should replace `dladdr` with an address-range compare
  (libpinsight's own .so range) — expected to cut several hundred ns.
- `full _Unwind walk ≈ glibc backtrace` confirms `_Unwind_Backtrace` is
  the right basis (it is what glibc uses internally); early termination is
  where all the win comes from.

## Validated properties

- cache == ground truth after every connect; replay == ground truth at
  every record (384-record adversarial tree × resync N ∈ {4, 32, 256};
  recursion; slot reuse with confirmed equal CFA; 2 threads).
- Drops: every gap detected via `ctx_seq`; replay context is *unknown*,
  never wrong; recovery exactly at the next resync; unknown window ≤ N.
- Region interleave (delta-before-region-op rule): nesting + anchors
  consistent across all records.
- Steady-state delta: ~2–3 unwind steps + K−1 confirm steps, 0 popped,
  0–2 entered.

## Carry into the integration (plan §3)

1. `CTX_CONFIRM_K` (default 3) goes into the design — single-frame match
   is not acceptable.
2. Tool-frame skip by address range of libpinsight.so, not dladdr.
3. Everything else lifts as prototyped: the merge, flush-on-failed-
   confirmation, forced resync on no-connect, resync semantics
   (replace, popped = whole previous path), ctx_seq.
