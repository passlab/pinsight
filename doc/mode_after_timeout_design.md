# `mode_after_timeout` — wall-clock guaranteed mode-switch deadline

**Status:** design only (no implementation yet) · 2026-06-25
**Related:** [trace_config_design.md](trace_config_design.md),
[control_thread_design.md](control_thread_design.md),
[four_mode_trace_design.md](four_mode_trace_design.md),
[rate-limit-tracing.md](rate-limit-tracing.md)

## 1. Summary

Add a per-process **wall-clock timeout** that *always* fires the
`trace_mode_after` global mode switch. The user specifies a number of seconds;
when that many seconds of wall time have elapsed, the configured mode switch
(e.g. `HIP → MONITORING`) is applied unconditionally — **even if no region has
reached its `max_num_traces` cap**. The timeout has the **highest priority** over
the region-count trigger policies (`first` / `all` / `anchor`): it is the one
trigger that is guaranteed to fire, so it acts as a hard ceiling on how long the
process stays in TRACING.

It is implemented entirely in the **control thread** (which already blocks on a
semaphore and already does `sem_timedwait` for INTROSPECT pauses), so it adds
**zero cost to the application hot path**.

This is a single per-process timer, not per-thread or per-region. The user has
confirmed per-process granularity is acceptable.

## 2. Motivation

`trace_mode_after` today is a count-driven, one-shot global switch: the **first**
lexgion (per-thread region) to reach `max_num_traces` flips the whole domain via
the `ma->fired` CAS latch in `pinsight_fire_mode_triggers()`
([src/pinsight.c:22](../src/pinsight.c#L22)). The planned trigger-policy menu
extends this to:

- **`first`** — current behavior: first region to cap fires (biased coverage, zero cost).
- **`all`** — fire only when every region *this thread* has seen has capped.
- **`anchor`** — fire when a user-named region (or set of regions) caps.

Both `all` and `anchor` have a **"never fires" failure mode**: if a region is
rare or never reached, the switch never happens and tracing (including the
otherwise-uncapped GPU activity stream — see
[amg2023_eval_overhead.md](code-memory/amg2023_eval_overhead.md)) runs for the
whole job. The timeout removes that failure mode and bounds worst-case trace
volume / overhead deterministically.

The timeout is also useful **standalone** (no count policy at all): "trace for T
seconds, then drop to MONITORING" — a time-windowed capture with **zero hot-path
cost**, since nothing on the app threads participates.

## 3. Semantics & priority

The mode switch fires at:

```
t_switch = min( t_policy , t_timeout )
```

where `t_policy` is when the configured count policy (`first`/`all`/`anchor`)
would fire, and `t_timeout` is `T` seconds after the timer arms.

- The existing `ma->fired` CAS latch makes this a **race that the first arrival
  wins**: whichever of {a capping region, the control-thread timer} trips the
  latch first applies the switch; the loser sees `fired == 1` and no-ops. No new
  priority/arbitration logic is required — the latch already gives
  "whichever-first" for free.
- "Highest priority" therefore means **guaranteed backstop**: the timeout is the
  trigger that cannot be skipped or misconfigured away. A count policy may
  *preempt* it (fire earlier), but the deadline always holds. The timeout never
  needs to override an already-applied switch (the switch is one-shot per cycle).
- If only the timeout is configured (no count policy / `max_num_traces == -1`),
  the switch fires at exactly `t_timeout`.

## 4. Where it lives — control thread, `sem_timedwait`

The control thread main loop currently blocks indefinitely:

```c
/* src/pinsight_control_thread.c:199 */
while (!control_shutdown) {
    while (sem_wait(&control_sem) == -1 && errno == EINTR)
        continue;
    ...
}
```

When a timeout is armed, the loop instead blocks with a deadline:

```c
while (!control_shutdown) {
    int rc;
    if (timeout_armed)
        rc = sem_timedwait(&control_sem, &mode_after_deadline);   /* CLOCK_REALTIME */
    else
        rc = sem_wait(&control_sem);

    if (rc == -1 && errno == ETIMEDOUT) {
        /* Deadline reached and the latch has not fired yet → fire it now. */
        fire_mode_after_timeout();   /* CAS ma->fired, apply modes, disarm */
        continue;
    }
    if (rc == -1 && errno == EINTR) continue;
    ... /* existing wakeup handling */
}
```

`fire_mode_after_timeout()` mirrors the non-INTROSPECT path of
`pinsight_fire_mode_triggers()`: CAS `ma->fired` 0→1 (bail if already fired),
write the per-domain target modes into `domain_default_trace_config[d].mode`
(atomic, `mode_change_fired` latch), then fall through to
`control_apply_all_modes()` so the HIP activity domain is actually
disabled/enabled. Because this runs *on the control thread*, it can call
`control_apply_all_modes()` directly without a self-wakeup.

The same INTROSPECT `sem_timedwait` pattern already exists
([src/pinsight_control_thread.c:139](../src/pinsight_control_thread.c#L139)), so
this is a well-trodden path — note it uses `CLOCK_REALTIME` (`clock_gettime` then
`+= seconds`), and the timeout version must match.

## 5. Config grammar

A new key in the `[Lexgion*]` section, alongside `trace_mode_after`:

```
[Lexgion.default]
    max_num_traces     = 50
    trace_mode_after   = HIP:MONITORING   # what to switch to (existing)
    mode_after_trigger = first | all | anchor   # when (count policy; default first) [separate doc]
    mode_after_timeout = 30               # NEW: seconds; switch by T no matter what
```

- Value is a positive integer/float number of **seconds** of wall time.
- Absent / `0` / negative ⇒ disabled (today's behavior; no timer armed).
- Parsed in [src/trace_config_parse.c:763](../src/trace_config_parse.c#L763)
  next to the existing `trace_mode_after` handling.

**Naming caution:** do **not** reuse the existing `trace_mode_after_t.introspect_timeout`
field — that is the INTROSPECT *pause duration* (how long to pause the app), a
different concept. This needs a distinct field.

## 6. Data structures

Extend `trace_mode_after_t`
([src/trace_config.h:231](../src/trace_config.h#L231)):

```c
typedef struct trace_mode_after {
    pinsight_domain_mode_t mode[MAX_NUM_DOMAINS];
    int introspect;
    int introspect_timeout;        /* INTROSPECT pause seconds (unchanged) */
    char introspect_script[256];
    volatile int fired;
    volatile unsigned int generation;

    /* NEW */
    int mode_after_timeout_sec;    /* >0: wall-clock deadline for the switch; 0 = off */
} trace_mode_after_t;
```

Control-thread-local (file-scope in `pinsight_control_thread.c`, written/read
only by the control thread — no locking):

```c
static int             timeout_armed = 0;
static struct timespec mode_after_deadline;  /* CLOCK_REALTIME */
```

## 7. Arming, firing, re-arming

- **Arm:** when the control thread first observes a config carrying
  `mode_after_timeout_sec > 0` (at startup config load, and after any
  SIGUSR1/inotify reload), it computes
  `clock_gettime(CLOCK_REALTIME, &deadline); deadline.tv_sec += sec;` and sets
  `timeout_armed = 1`. Re-arming on reload lets the user add/extend the timeout
  live.
- **Clock start reference (decision):** arm relative to **when the timer is first
  set up at control-thread start** (≈ process start / constructor). This is the
  simplest per-process definition and matches "trace for the first T seconds of
  the process." (Alternative: arm at *first trace* to exclude GPU/LTTng init —
  more faithful but needs a one-shot signal from the first
  `lexgion_post_trace_update`. Recommend deferring; init is ~seconds and the
  timeout is meant as a coarse ceiling.)
- **Fire:** on `ETIMEDOUT`, run `fire_mode_after_timeout()` (Section 4), then set
  `timeout_armed = 0` so it does not re-fire.
- **Already fired by a count policy:** the timer still wakes at the deadline, the
  CAS in `fire_mode_after_timeout()` sees `fired == 1`, no-ops, and disarms.
  Harmless. (Optionally disarm eagerly when a count trigger fires, but not
  required for correctness.)
- **Cyclic INTROSPECT re-arm:** when a cycle completes and `ma->fired` is reset +
  `generation++`
  ([src/pinsight_control_thread.c:265](../src/pinsight_control_thread.c#L265)),
  re-compute the deadline so the timeout applies to each cycle, not only the
  first. Without this, the timeout only guards cycle 1.

## 8. Interaction with existing mechanisms

- **CAS latch (`ma->fired`):** single arbiter for "who fires the switch." The
  timeout is just another contender. No double-switch possible.
- **Immediate-apply path:** count triggers apply the mode in the *calling app
  thread* for low latency
  ([src/pinsight.c:42](../src/pinsight.c#L42)) and then wake the control thread.
  The timeout applies the mode in the *control thread* directly. Both end at
  `control_apply_all_modes()`. The `domain_default_trace_config[].mode` write is
  atomic/`volatile`, so app threads observe the new mode on their next
  `PINSIGHT_SHOULD_TRACE` check regardless of which thread flipped it.
- **HIP activity capture:** the whole point — flipping HIP to MONITORING via
  `control_apply_all_modes()` → `pinsight_control_hip_apply_mode()` disables the
  activity domain (capture is tied to TRACING), stopping the uncapped GPU
  activity stream. This is the documented overhead lever
  ([amg2023_eval_overhead.md](code-memory/amg2023_eval_overhead.md)).
- **Config reload:** reload re-parses and can change/clear the timeout; re-arm
  logic in Section 7 handles it.

## 9. Edge cases

- **Timeout but no `trace_mode_after`:** nothing to switch to. Treat as a config
  error / no-op (warn at parse time).
- **`mode_after_timeout` with INTROSPECT:** the timeout here means "fire the
  introspect+resume action by T." Simplest first cut: support the timeout only
  for the **non-INTROSPECT** mode switch; reject or ignore it when
  `introspect == 1` (INTROSPECT already has its own pause-timeout semantics).
  Document the restriction.
- **Spurious `sem_timedwait` wakeups / real wakeups before deadline:** a normal
  wakeup (mode change, reload) returns 0, not `ETIMEDOUT`; the loop processes it
  and re-enters the wait with the *same* (still-future) deadline. The timer is
  not lost. Only `ETIMEDOUT` fires the switch.
- **Shutdown before deadline:** `control_shutdown` + a final `sem_post` wake the
  thread (returns 0/EINTR, not ETIMEDOUT); it exits normally. No switch fired.

## 10. Pros / cons

**Pros**
- Guarantees the switch always fires → makes `all`/`anchor` safe (removes their
  "never fires" failure mode); they no longer need bespoke fallbacks.
- Bounds worst-case TRACING duration / trace volume / overhead deterministically
  — a production safety knob.
- **Zero application hot-path cost:** lives entirely in the control thread; app
  threads are untouched.
- Reuses the control thread's existing `sem_timedwait` infrastructure; the CAS
  latch already handles the race — minimal new code.
- Usable standalone as a simple time-windowed tracing policy.

**Cons / trade-offs**
- **Wall-clock is non-deterministic across machines/runs** — a faster node
  reaches T after fewer samples. Accepted: the timeout is a coarse ceiling, not a
  sampling spec. (A count-based "timeout" would be reproducible but requires a
  global hot-path counter — explicitly **out of scope**, see Section 11.)
- Must remember to **re-arm on cyclic INTROSPECT** or it only guards cycle 1.
- One more config knob and one new struct field.

## 11. Explicitly out of scope

- **Count/iteration-based timeout** (fire after N total invocations): would need a
  global atomic incremented on the hot path — defeats the zero-cost property.
  Wall-clock only, per this design.
- **Per-region / per-thread timeouts:** this is a single per-process timer by
  design (user-confirmed). Per-region activity rate-limiting remains a separate,
  unimplemented item (correlation-id gating).
- **First-trace-relative clock start:** deferred (Section 7); start is
  control-thread-start-relative for now.

## 12. Open decisions (for the user)

1. **Clock start:** control-thread-start (recommended, simplest) vs first-trace.
2. **Units:** integer seconds (recommended) vs fractional seconds.
3. **INTROSPECT + timeout:** reject (recommended first cut) vs support.
4. **Eager disarm** when a count policy fires: nice-to-have vs rely on the
   harmless CAS no-op at deadline.
