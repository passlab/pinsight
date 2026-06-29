# `window_timeout` — wall-clock guaranteed end-of-tracing-window deadline

**Status:** design only (no implementation yet) · drafted 2026-06-25, revised 2026-06-26
**Related:** [trace_config_design.md](trace_config_design.md),
[control_thread_design.md](control_thread_design.md),
[four_mode_trace_design.md](four_mode_trace_design.md),
[rate-limit-tracing.md](rate-limit-tracing.md)

> **Naming note (2026-06-26).** This mechanism was originally drafted as
> `mode_after_timeout`. It is now framed around the concept of a **tracing
> window** — a bounded stretch of TRACING that ends, and transitions to another
> mode, when *either* trigger trips. The config key is **`window_timeout`**. The
> file name is kept for link/history stability. At implementation time the
> pre-existing field `introspect_timeout` is **renamed to
> `introspect_pause_duration`** (it is the INTROSPECT *pause length*, a different
> concept from this window deadline) to avoid confusion.

## 1. Summary

A **window** is a maximal stretch of execution during which a domain stays in one
mode (TRACING / MONITORING / STANDBY / OFF). The run is a sequence of windows, and
each **mode switch** is simply the boundary between one window and the next. The
**TRACING window** is the one this design bounds: it ends — and transitions the
domain to another mode (e.g. `HIP → MONITORING`) per `trace_mode_after` — when
either trigger trips:

- the **count trigger**: some region reaches `max_num_traces` (today's behavior), or
- the new **time trigger** `window_timeout`: a per-process wall-clock deadline.

In cyclic INTROSPECT these windows repeat and are numbered — the existing
`generation` counter ([src/trace_config.h:240](../src/trace_config.h#L240)) is
effectively the window index.

`window_timeout` adds the time trigger: when `T` seconds of wall time elapse, the
configured mode switch is applied **unconditionally** — even if no region has
reached its `max_num_traces` cap. It is the trigger that is *guaranteed* to fire,
so it acts as a hard ceiling on how long the process stays in TRACING within a
window.

It is implemented entirely in the **control thread** (which already blocks on a
semaphore and already does `sem_timedwait` for INTROSPECT pauses), so it adds
**zero cost to the application hot path**.

This is a single per-process timer, not per-thread or per-region. Per-process
granularity is confirmed acceptable.

### 1.1 Window lifecycle (the overall model)

A **window** is a maximal stretch of execution during which a domain stays in one
mode (TRACING / MONITORING / STANDBY / OFF); a **mode switch** is the boundary
between consecutive windows. The TRACING window has one lifecycle, and the two
triggers and INTROSPECT are all points on it:

1. **The window runs** in TRACING.
2. **The window ends** at `t_end = min(t_count, t_timeout)` (Section 3) — i.e. on
   the *first* of:
   - the **time trigger** `window_timeout` (T wall seconds elapse), and/or
   - the **count trigger** `max_num_traces`, fired via the chosen
     `first` / `all` policy (`anchor` reserved for future use).

   The `ma->fired` CAS latch makes this a first-wins race; the loser no-ops.
3. **At window end, INTROSPECT runs if configured** — the analysis script is
   spawned and (if `introspect_pause_duration != 0`) the application pauses, then
   resumes. This happens *before* the mode transition.
4. **The mode transition** (`trace_mode_after`) is applied — the boundary that
   starts the next window. Two shapes:
   - **Indefinite next window** (e.g. → `HIP:MONITORING`): a MONITORING (or
     STANDBY/OFF) window begins with **no end-trigger configured**, so it runs
     indefinitely and there is no further auto-transition. This is the
     overhead-ceiling use (drop to MONITORING and stay there). This is *not* a
     special case — it is just a window whose end is unbounded.
   - **Cyclic resume** (the next window's mode is **TRACING**): a new TRACING
     window begins — the window index `generation` advances, the `fired` /
     `mode_change_fired` latches reset
     ([src/pinsight_control_thread.c:252-274](../src/pinsight_control_thread.c#L252-L274)),
     and we are back at step 1 with the window timer re-armed (Section 7). In the
     auto-trigger path, repeating TRACING windows are driven by INTROSPECT
     resuming to TRACING; the gap between them is the introspect pause, not a
     separate MONITORING period.

So, in one line: **end-trigger(s) → INTROSPECT (if configured) → mode transition →
the next window begins (indefinite, or a fresh TRACING window if cyclic).**
Everything below elaborates one step of this lifecycle.

**Which windows have auto end-triggers (current scope):** today `window_timeout`
and `max_num_traces` end the **TRACING window** specifically. A MONITORING /
STANDBY / OFF window has no configured end-trigger — it ends only by an external
event (SIGUSR1 / inotify config reload) or runs indefinitely. The terminology
cleanly invites a future generalization — let *any* window carry its own
`window_timeout` so that, e.g., a MONITORING window auto-flips back to TRACING
after T seconds (a general timed mode state machine) — but that is **out of
current scope**.

## 2. Motivation

`trace_mode_after` today is a count-driven, one-shot global switch: the **first**
lexgion (per-thread region) to reach `max_num_traces` flips the whole domain via
the `ma->fired` CAS latch in `pinsight_fire_mode_triggers()`
([src/pinsight.c:22](../src/pinsight.c#L22)). The planned trigger-policy menu
extends this to:

- **`first`** — current behavior: first region to cap fires (biased coverage, zero cost). *(implemented)*
- **`all`** — fire only when every region *this thread* has seen has capped. *(implemented)*
- **`anchor`** — fire when a user-named region (or set of regions) caps. *(reserved — not implemented in this work)*

**Implemented scope:** only `first` and `all` are built now. `anchor` is a
**reserved** policy value, designed for additive extension: the trigger policy is
a parsed enum (e.g. `mode_after_trigger_t { TRIGGER_FIRST, TRIGGER_ALL,
TRIGGER_ANCHOR }`), so adding `anchor` later is just a new accepted keyword plus
one branch in the fire logic — no change to the data model, the window, or the
timeout machinery. The parser should reject the `anchor` keyword for now with a
clear "not yet implemented" message rather than silently ignoring it.

**`all` policy cost:** the `all` gate (`pinsight_all_seen_lexgions_capped`) scans
this thread's lexgion cache, but is reached **only at a region's cap edge** (once
per finite-cap region per window — a capped region's `trace_bit` goes 0, so it
stops calling `lexgion_post_trace_update`), never per traced invocation. That is
O(N) per scan, **O(N²) per window** (N = finite-cap lexgions a thread has seen,
bounded by `MAX_NUM_LEXGIONS`); microseconds/window for realistic N, paid only
during the brief cap-out phase. The default `first` policy short-circuits to a
single int compare. If a pathological case (hundreds of capped regions in tight
cyclic windows) ever arises, swap the scan for two per-thread counters
(`num_finite_seen`/`num_finite_capped`, the latter reset on the generation bump)
for an O(1) gate. Documented inline on the helper in `src/pinsight.c`.

Both `all` and `anchor` (once added) have a **"never fires" failure mode**: if a region is
rare or never reached, the switch never happens and tracing (including the
otherwise-uncapped GPU activity stream — see
[amg2023_eval_overhead.md](code-memory/amg2023_eval_overhead.md)) runs for the
whole job. `window_timeout` removes that failure mode and bounds worst-case trace
volume / overhead deterministically.

The time trigger is also useful **standalone** (no count policy at all): "trace
for T seconds, then drop to MONITORING" — a time-windowed capture with **zero
hot-path cost**, since nothing on the app threads participates.

## 3. Semantics & priority

The window ends (mode switch fires) at:

```
t_end = min( t_count , t_timeout )
```

where `t_count` is when the configured count policy (`first`/`all`; `anchor`
reserved) would fire, and `t_timeout` is `T` seconds after the timer arms.

- The existing `ma->fired` CAS latch makes this a **race that the first arrival
  wins**: whichever of {a capping region, the control-thread timer} trips the
  latch first ends the window; the loser sees `fired == 1` and no-ops. No new
  priority/arbitration logic is required — the latch already gives
  "whichever-first" for free.
- "Guaranteed" therefore means **backstop**: `window_timeout` is the trigger that
  cannot be skipped or misconfigured away. A count policy may *preempt* it (fire
  earlier), but the deadline always holds. The timeout never needs to override an
  already-applied switch (the switch is one-shot per window).
- If only the timeout is configured (no count policy / `max_num_traces == -1`),
  the window ends at exactly `t_timeout`.

## 4. Where it lives — control thread, `sem_timedwait` + synthesized wakeup

The control thread main loop currently blocks indefinitely:

```c
/* src/pinsight_control_thread.c:203 */
while (!control_shutdown) {
    while (sem_wait(&control_sem) == -1 && errno == EINTR)
        continue;
    ...
    /* 2 & 3. Auto-trigger: Mode Change and/or Introspection  (lines 227-275) */
    /* 4.    control_apply_all_modes()                          (lines 277-282) */
}
```

When a window timeout is armed, the loop instead blocks with a deadline, and on
`ETIMEDOUT` it **synthesizes the same wakeup the count path would have produced**
and falls through to the *existing* handler — rather than duplicating that logic
in a separate function:

```c
while (!control_shutdown) {
    int rc;
    if (window_timer_armed)
        rc = sem_timedwait(&control_sem, &window_deadline);   /* CLOCK_REALTIME */
    else
        rc = sem_wait(&control_sem);

    if (rc == -1 && errno == EINTR) continue;

    int reason;
    if (rc == -1 && errno == ETIMEDOUT) {
        /* Deadline reached. Win the latch; if a count policy already fired this
         * window, the CAS no-ops and we just disarm (Section 7). */
        trace_mode_after_t *ma = window_timer_ma;   /* the armed action */
        int expected = 0;
        if (!__atomic_compare_exchange_n(&ma->fired, &expected, 1, 0,
                                         __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)) {
            window_timer_armed = 0;   /* already fired by count; nothing to do */
            continue;
        }
        pending_mode_action = ma;
        /* Synthesize the wakeup the app thread would have posted, choosing the
         * INTROSPECT vs plain-mode-switch payload from the action itself. */
        reason = ma->introspect ? PINSIGHT_WAKEUP_INTROSPECT
                                : PINSIGHT_WAKEUP_MODE_CHANGE;
    } else {
        if (control_shutdown) break;
        reason = __atomic_exchange_n(&pending_wakeup_reason, 0, __ATOMIC_SEQ_CST);
    }

    /* ... existing handler: CONFIG_RELOAD, INTROSPECT/MODE_CHANGE (which calls
     * control_execute_introspect + applies modes + cyclic reset), then
     * control_apply_all_modes(). Re-arm/disarm the window timer here. */
}
```

**Why synthesize instead of a standalone `fire_mode_after_timeout()`:** the
timeout fires *on the control thread*, and so does INTROSPECT
(`control_execute_introspect()` at
[src/pinsight_control_thread.c:232](../src/pinsight_control_thread.c#L232),
reached from the handler block at
[src/pinsight_control_thread.c:227-282](../src/pinsight_control_thread.c#L227-L282)).
By setting `pending_mode_action` + a `reason` and falling through, the
timeout-fired path runs **byte-identical handling** to the count-fired path —
INTROSPECT (script + pause/resume), the per-domain mode-apply loop, and the
cyclic generation reset — with no duplicated logic. This is what makes INTROSPECT
support (Section 9) essentially free.

This race-then-handle pattern mirrors the immediate-apply done by the count path
in the app thread ([src/pinsight.c:42-67](../src/pinsight.c#L42-L67)); here the
CAS + dispatch simply happen on the control thread. The INTROSPECT pause path
already uses `sem_timedwait` with `CLOCK_REALTIME`
([src/pinsight_control_thread.c:139](../src/pinsight_control_thread.c#L139)), so
this is a well-trodden clock; the window timer must match it (`clock_gettime`
then `+= seconds`).

## 5. Config grammar

A new key in the `[Lexgion*]` section, alongside `trace_mode_after`:

```
[Lexgion.default]
    max_num_traces   = 50              # end window by count (existing)
    trace_mode_after = HIP:MONITORING  # what to switch to at window end (existing)
    mode_after_trigger = first | all            # count policy (default first); 'anchor' reserved, not yet implemented
    window_timeout   = 30              # NEW: end the window after T wall seconds, no matter what
```

- Value is a positive **integer** number of **seconds** of wall time.
- **Default = disabled (`0`).** Absent / `0` / negative ⇒ no timer armed and zero
  added cost; the window ends by count policy only. The time trigger is strictly
  opt-in (today's behavior is preserved when the key is absent).
- **`all` (or a future `anchor`) with no `window_timeout`:** legal, but the window
  may **never end** if a region is rare or never re-reached (the never-fires mode,
  Section 2). The parser **emits a warning** recommending a `window_timeout`, then
  proceeds. It deliberately does **not** hard-error (an unbounded `all` window may
  be intentional) and does **not** inject an arbitrary default ceiling (a
  wall-clock default would be non-deterministic and surprising). `first` always
  fires, so it needs no such warning.
- Parsed in [src/trace_config_parse.c:763](../src/trace_config_parse.c#L763)
  next to the existing `trace_mode_after` handling (the `parse_trace_mode_after`
  helper is at [src/trace_config_parse.c:90](../src/trace_config_parse.c#L90)).

**Naming caution:** this is distinct from the INTROSPECT *pause duration*. That
pre-existing field, currently `trace_mode_after_t.introspect_timeout`, is
**renamed to `introspect_pause_duration`** as part of this work (see Section 6) —
`window_timeout` = *when the window ends*; `introspect_pause_duration` = *how long
the app pauses once INTROSPECT runs*. The two are orthogonal and may coexist.

### 5.1 Environment variable (`PINSIGHT_TRACE_WINDOW`, was `PINSIGHT_TRACE_RATE`)

`max_num_traces` and `trace_mode_after` are already reachable without a config
file via this variable, parsed in `setup_trace_config_env()`
([src/trace_config.c:142-171](../src/trace_config.c#L142-L171)). The variable name
is changed from `PINSIGHT_TRACE_RATE` to **`PINSIGHT_TRACE_WINDOW`**: it never
carried only a rate — it bundles every knob that *shapes a tracing window* (start,
count cap, sampling rate, and now the wall-clock timeout + end-of-window
transition), so the new name reflects its actual job and the window concept.
`window_timeout` joins the bundle as a new positional field inserted **after
`rate` and before the mode-after tail**:

```
# OLD name + OLD grammar
PINSIGHT_TRACE_RATE   = start : max : rate [ : mode_after_string ]
# NEW name + NEW grammar (window_timeout is the new 4th field)
PINSIGHT_TRACE_WINDOW = start : max : rate : window_timeout [ : mode_after_string ]
```

Read as: *the window starts at `start`, caps at `max` traces, is sampled at
`rate`, times out after `window_timeout` s, then performs `mode_after`.*

- `window_timeout` is an **integer** seconds value; `0` (or absent) = disabled.
- To set a mode-after **without** a window timeout, put `0` in the 4th field, e.g.
  `0:50:1:0:HIP:MONITORING`. To set a window timeout with no count cap, use
  `0:-1:1:30:HIP:MONITORING`.
- Example — cap at 50 traces *or* 30 s, then HIP→MONITORING:
  `PINSIGHT_TRACE_WINDOW=0:50:1:30:HIP:MONITORING`.

**Backward compatibility.** `PINSIGHT_TRACE_RATE` is **retained as a deprecated
alias**. `setup_trace_config_env()` reads `PINSIGHT_TRACE_WINDOW` first; if unset,
it falls back to `PINSIGHT_TRACE_RATE` and emits a one-line deprecation warning.
Both names use the **same (new) grammar** — i.e. the alias also expects the 4th
`window_timeout` field. The alias may be removed in a later release.

**⚠ Breaking change (grammar, both names).** The 4th colon-separated field was
previously the *mode_after_string*; it is now `window_timeout`, and the mode-after
tail shifts to the 5th field. Any existing value that used a 4th field (e.g.
`0:50:1:HIP:MONITORING`) must gain the new field (`0:50:1:0:HIP:MONITORING`),
whether set via the new name or the deprecated alias. Acceptable for a research
tool, but must be noted in the changelog and the config-format doc.

**Parser change** (in `setup_trace_config_env()`):

```c
char *win_env = getenv("PINSIGHT_TRACE_WINDOW");
if (!win_env) {
    win_env = getenv("PINSIGHT_TRACE_RATE");      /* deprecated alias */
    if (win_env)
        fprintf(stderr, "PInsight WARNING: PINSIGHT_TRACE_RATE is deprecated; "
                        "use PINSIGHT_TRACE_WINDOW (same grammar)\n");
}
```

then read a 4th integer with
`sscanf(win_env, "%d:%d:%d:%d", &start, &max, &rate, &window_timeout)` and, when
`count >= 4`, store it in
`lexgion_default_trace_config->mode_after.window_timeout_sec`; finally advance past
**four** colons (was three) before handing the remaining tail to
`parse_trace_mode_after()`
([src/trace_config.c:160-170](../src/trace_config.c#L160-L170)).

**Unaffected:**
- The per-domain mode override `PINSIGHT_TRACE_<DOMAIN>`
  ([src/trace_config.c:112-140](../src/trace_config.c#L112-L140)) — mode only, no
  change.
- The INTROSPECT sub-grammar inside the mode-after string
  (`INTROSPECT:<pause>:<script>[:<resume_mode>]`) — the `introspect_timeout` →
  `introspect_pause_duration` rename is internal to `parse_trace_mode_after`; the
  `<pause>` token is unchanged, so existing INTROSPECT strings keep working in both
  the env var and the config file.

The companion config-format reference
([PINSIGHT_TRACE_CONFIG_FORMAT.md](PINSIGHT_TRACE_CONFIG_FORMAT.md)) must be
updated for both the new `[Lexgion*]` key and this `PINSIGHT_TRACE_RATE` field.

## 6. Data structures

Extend `trace_mode_after_t`
([src/trace_config.h:231](../src/trace_config.h#L231)), renaming the existing
pause field and adding the window deadline:

```c
typedef struct trace_mode_after {
    pinsight_domain_mode_t mode[MAX_NUM_DOMAINS];
    int introspect;
    int introspect_pause_duration; /* RENAMED from introspect_timeout: INTROSPECT
                                      pause seconds (>0 pause N, 0 none, <0 wait) */
    char introspect_script[256];
    volatile int fired;
    volatile unsigned int generation;

    /* NEW */
    int window_timeout_sec;        /* >0: wall-clock deadline ending the tracing
                                      window; 0/absent = off */
} trace_mode_after_t;
```

The `introspect_timeout` rename touches its uses at
[src/pinsight_control_thread.c:122-142](../src/pinsight_control_thread.c#L122-L142)
and its parse site in `trace_config_parse.c`; mechanical and isolated.

Control-thread-local (file-scope in `pinsight_control_thread.c`, written/read
only by the control thread — no locking):

```c
static int                 window_timer_armed = 0;
static struct timespec     window_deadline;     /* CLOCK_REALTIME */
static trace_mode_after_t *window_timer_ma = NULL; /* action to fire on ETIMEDOUT */
```

## 7. Arming, firing, re-arming

- **Arm:** when the control thread first observes a config carrying
  `window_timeout_sec > 0` (at startup config load, and after any
  SIGUSR1/inotify reload), it records `window_timer_ma`, computes
  `clock_gettime(CLOCK_REALTIME, &window_deadline); window_deadline.tv_sec += sec;`
  and sets `window_timer_armed = 1`. Re-arming on reload lets the user add/extend
  the timeout live.
- **Clock start reference (DECIDED):** arm relative to **control-thread start**
  (≈ process start / library load). The control thread starts when the
  application loads the library, so this matches "trace for the first T seconds of
  the process." (The first-trace-relative alternative — excluding GPU/LTTng init —
  was considered and rejected as unnecessary; init is ~seconds and the window is a
  coarse ceiling.)
- **Units (DECIDED):** integer seconds.
- **Fire:** on `ETIMEDOUT`, CAS `ma->fired` 0→1, then dispatch via the synthesized
  wakeup (Section 4), then set `window_timer_armed = 0` so it does not re-fire
  within the same window.
- **Already fired by a count policy (DECIDED: rely on the CAS no-op):** the timer
  still wakes at the deadline, the CAS sees `fired == 1`, no-ops, and disarms.
  Harmless — one spurious wakeup of an otherwise-idle, zero-CPU thread. We do
  **not** eagerly disarm from the app thread when a count policy fires: the count
  path runs on the app thread ([src/pinsight.c:42](../src/pinsight.c#L42)) while
  `window_timer_armed` is control-thread-local, so eager disarm would require
  app→control signalling for no real benefit.
- **Cyclic re-arm (this is the real correctness guard):** when a window completes
  and resumes to TRACING — `ma->fired` reset + `generation++` at
  [src/pinsight_control_thread.c:265-274](../src/pinsight_control_thread.c#L265-L274)
  — **re-compute `window_deadline` and set `window_timer_armed = 1`** so the
  timeout applies to *each* window, not only the first. This re-arm is also what
  makes "rely on the CAS no-op" safe: because every window reset replaces any
  stale deadline, a leftover timer from a count-ended window can never misfire in
  a later one. Without re-arm, the timeout would only guard window 1.

## 8. Pause time is excluded automatically

During an INTROSPECT pause, the control thread is blocked *inside*
`control_execute_introspect` on its own `sem_timedwait`
([src/pinsight_control_thread.c:139](../src/pinsight_control_thread.c#L139)) —
not in the main-loop window wait. So the window clock does not advance during a
pause. Because we re-compute the deadline on resume (Section 7), `window_timeout`
measures **tracing-window wall time**, with pause time naturally excluded. This is
the desired semantics.

## 9. INTROSPECT support (DECIDED: supported)

INTROSPECT and the plain mode-switch are two payloads of the *same* trigger (a
region hitting `max_num_traces`), so the window deadline must be able to fire
*either*. Because the timeout fires on the control thread — the same thread that
runs INTROSPECT — this is clean: the synthesized wakeup (Section 4) selects
`PINSIGHT_WAKEUP_INTROSPECT` when `ma->introspect == 1`, and the existing handler
runs `control_execute_introspect()` (script + pause-for-`introspect_pause_duration`
+ resume), then the mode-apply and cyclic-reset, exactly as the count path does.

- `window_timeout` and `introspect_pause_duration` are **orthogonal and coexist**:
  e.g. "if no region caps within `window_timeout = 30`s, run the introspect script
  and pause for `introspect_pause_duration = 5`s, then resume." No conflict.
- **Caveat to accept:** with INTROSPECT, a window timeout can now *pause the
  application* (and spawn the analysis script), not merely flip a domain to
  MONITORING. That is inherent to a deadline-driven introspection and is the
  intended behavior.

(The earlier draft's "non-INTROSPECT first cut" restriction is **removed**.)

## 10. Interaction with existing mechanisms

- **Arbitration (`mode_change_fired`, not just `ma->fired`) — corrected by
  testing 2026-06-29.** The original design assumed the per-config `ma->fired` CAS
  is the single arbiter for "who ends the window." In practice that is **not
  sufficient**: default lexgions resolve to per-domain *copies* of the window's
  `mode_after` (`lexgion_domain_default_trace_config[domain] = *lexgion_default`),
  so the count path CASes a *different* `fired` field than the timer (which arms
  off `lexgion_default`). With only the `ma->fired` check, a count policy that
  ended the window did not stop the timer from also firing — harmless for a plain
  mode switch (the **domain-level `mode_change_fired`** latch, set by both the
  immediate count path at [src/pinsight.c:52](../src/pinsight.c#L52) and the
  control-thread path, kept the actual switch idempotent → still exactly one
  switch), but for **INTROSPECT it would double-run the script/pause**. Fix: the
  timer's `ETIMEDOUT` handler now also calls `window_already_ended(ma)`, which
  treats `domain_default_trace_config[d].mode_change_fired` over the action's
  target domains as the real cross-config arbiter, and no-ops if the window
  already ended. (Narrow residual: pure INTROSPECT with no resume modes has no
  domain target and is not arbitrated this way — an accepted edge.)
- **Immediate-apply path:** count triggers apply the mode in the *calling app
  thread* for low latency
  ([src/pinsight.c:42-57](../src/pinsight.c#L42-L57)) and then wake the control
  thread. The timeout applies the mode in the *control thread* directly via the
  synthesized wakeup. Both end at `control_apply_all_modes()`
  ([src/pinsight_control_thread.c:164](../src/pinsight_control_thread.c#L164)).
  The `domain_default_trace_config[].mode` write is atomic/`volatile`, so app
  threads observe the new mode on their next `PINSIGHT_SHOULD_TRACE` check
  regardless of which thread flipped it.
- **HIP activity capture:** the whole point — flipping HIP to MONITORING via
  `control_apply_all_modes()` → `pinsight_control_hip_apply_mode()` disables the
  activity domain (capture is tied to TRACING), stopping the uncapped GPU
  activity stream. This is the documented overhead lever
  ([amg2023_eval_overhead.md](code-memory/amg2023_eval_overhead.md)).
- **Config reload:** reload re-parses and can change/clear `window_timeout`; the
  arm/re-arm logic in Section 7 handles it.

## 11. Edge cases

- **`window_timeout` but no `trace_mode_after`:** nothing to switch to. Treat as a
  config error / no-op (warn at parse time).
- **Spurious `sem_timedwait` wakeups / real wakeups before the deadline:** a
  normal wakeup (mode change, reload) returns 0, not `ETIMEDOUT`; the loop
  processes it and re-enters the wait with the *same* (still-future) deadline. The
  timer is not lost. Only `ETIMEDOUT` ends the window.
- **Shutdown before the deadline:** `control_shutdown` + a final `sem_post` wake
  the thread (returns 0/EINTR, not ETIMEDOUT); it exits normally. No switch fired.

## 12. Pros / cons

**Pros**
- Guarantees the window ends → makes the `all` policy (and a future `anchor`)
  safe (removes the "never fires" failure mode); they no longer need bespoke
  fallbacks.
- Bounds worst-case TRACING duration / trace volume / overhead deterministically
  — a production safety knob.
- **Zero application hot-path cost:** lives entirely in the control thread; app
  threads are untouched.
- Reuses the control thread's existing `sem_timedwait` infra and, via the
  synthesized wakeup, the existing INTROSPECT/mode-apply/cyclic-reset handler —
  minimal new code, INTROSPECT support for free.
- Usable standalone as a simple time-windowed tracing policy.

**Cons / trade-offs**
- **Wall-clock is non-deterministic across machines/runs** — a faster node reaches
  T after fewer samples. Accepted: the window timeout is a coarse ceiling, not a
  sampling spec. (A count-based "timeout" would be reproducible but requires a
  global hot-path counter — explicitly **out of scope**, see Section 13.)
- Must remember to **re-arm on each cyclic window** or it only guards window 1.
- One more config knob and one new struct field (plus the `introspect_timeout`
  rename).
- With INTROSPECT, a timeout can pause the application (Section 9 caveat).

## 13. Explicitly out of scope

- **Count/iteration-based timeout** (fire after N total invocations): would need a
  global atomic incremented on the hot path — defeats the zero-cost property.
  Wall-clock only, per this design.
- **Per-region / per-thread windows:** this is a single per-process timer by
  design. Per-region activity rate-limiting remains a separate, unimplemented item
  (correlation-id gating).

## 14. Resolved decisions (2026-06-26)

1. **Clock start:** control-thread-start (≈ process start). ✔
2. **Units:** integer seconds. ✔
3. **INTROSPECT + window timeout:** supported, via the synthesized-wakeup shape
   (Section 4/9). ✔
4. **Eager disarm when a count policy fires:** no — rely on the harmless CAS
   no-op; cyclic re-arm is the real guard (Section 7). ✔
5. **Concept/term:** "tracing window"; config key `window_timeout`; pre-existing
   `introspect_timeout` renamed to `introspect_pause_duration`. ✔
