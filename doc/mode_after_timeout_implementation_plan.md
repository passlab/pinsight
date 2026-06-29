# `window_timeout` — implementation plan

**Status:** COMPLETED · planned 2026-06-26, landed 2026-06-29 (branch
`feature/window-timeout-mode-trigger`). Phase 6 `all` was fully implemented (not
just scaffolded) per the per-thread "first thread whose seen regions all capped"
semantics. See the checklist in §11.
**Design:** [mode_after_timeout_design.md](mode_after_timeout_design.md)
**Targets:** branch `hip-rocm-support` (current dev line); build dirs `build_hip/`
(gcc) for the control-thread/parser work, validated on Tuolumne.

## 0. Scope

**In scope (the designed feature):** a per-process wall-clock `window_timeout`
that ends the current TRACING window via the control thread, firing the existing
`trace_mode_after` transition (mode switch *or* INTROSPECT). Plus the two settled
renames and the env-var rename/alias.

**In scope, narrow:** rename `introspect_timeout` → `introspect_pause_duration`
(prerequisite); env var `PINSIGHT_TRACE_RATE` → `PINSIGHT_TRACE_WINDOW` + alias.

**Companion, separate design (Phase 6, do NOT block on it):** the
`mode_after_trigger = first | all` count policy. Only `first` exists today (the
CAS latch). Implementing `all` (fire when every region this thread has seen has
capped) needs its own design for the per-thread "all capped" bookkeeping; this
plan adds only the *parser scaffolding* (accept `first`/`all`, reject `anchor`)
and the enum, leaving `all`'s firing logic to its own task. `window_timeout` does
**not** depend on it — it backstops whatever count policy is in effect.

**Out of scope:** `anchor` policy; per-region/per-window timers; count-based
timeout (Section 13 of the design).

## 1. Ordering / dependency graph

```
Phase 0 (rename)  ──► Phase 1 (struct) ──► Phase 2 (file parse)   ──┐
                                      └──► Phase 3 (env var)       ──┤
                                                                    ├─► Phase 4 (control-thread timer) ─► Phase 5 (test)
Phase 6 (mode_after_trigger enum) — independent, can land before or after
```

Phases 0–4 are sequential. Do them in order; each compiles and is independently
testable. Phase 5 (tests) and Phase 6 (count policy) can interleave.

## 2. Phase 0 — rename `introspect_timeout` → `introspect_pause_duration`

Mechanical, isolated; do first so later diffs are clean.

- **[src/trace_config.h:235](../src/trace_config.h#L235)** — rename the struct
  field; update the comment.
- **[src/trace_config_parse.c:104](../src/trace_config_parse.c#L104)** — the
  INTROSPECT parse writes `out->introspect_timeout`; rename. The *config/env
  string grammar is unchanged* (`INTROSPECT:<pause>:<script>`); only the C field
  name changes.
- **[src/pinsight_control_thread.c:122-142](../src/pinsight_control_thread.c#L122-L142)**
  — `control_execute_introspect()` reads the field three times; rename.
- `grep -rn 'introspect_timeout' src/ test/` to confirm zero stragglers.

**Verify:** `build_hip/` compiles; existing INTROSPECT config/test still runs
identically (no behavior change).

## 3. Phase 1 — data structure

**[src/trace_config.h:231](../src/trace_config.h#L231)** — add to
`trace_mode_after_t`, after the rename:

```c
int window_timeout_sec;   /* >0: wall-clock deadline ending the TRACING window;
                             0/absent = disabled (default). */
```

Default-zero is automatic for `= {0}`-initialized configs and the
`lexgion_default_trace_config`. No initializer changes needed beyond confirming
defaults are zeroed.

**Verify:** compiles; `sizeof(trace_mode_after_t)` grows by one int; nothing reads
the field yet.

## 4. Phase 2 — config-file parsing (`window_timeout` key)

**[src/trace_config_parse.c:763](../src/trace_config_parse.c#L763)** — add a
`window_timeout` branch in the `[Lexgion*]` key dispatch, beside
`max_num_traces`/`trace_mode_after`:

```c
} else if (strcmp(key, "window_timeout") == 0) {
    int v = atoi(val);
    for (int ci = 0; ci < cfg_count; ci++)
        cfgs[ci]->mode_after.window_timeout_sec = v;
}
```

**GOTCHA — ordering vs. `trace_mode_after`.** The `trace_mode_after` branch does
`trace_mode_after_t parsed = {0}; ... cfgs[ci]->mode_after = parsed;` — a **full
struct overwrite** that already preserves `generation`/`fired`
([src/trace_config_parse.c:765-774](../src/trace_config_parse.c#L765-L774)). It
must **also preserve `window_timeout_sec`**, or a `window_timeout` line appearing
*before* `trace_mode_after` in the file gets wiped. Add:

```c
parsed.window_timeout_sec = cfgs[ci]->mode_after.window_timeout_sec;
```

next to the existing `parsed.generation = …; parsed.fired = …;` lines.

**Never-fires warning (design §5).** When a config sets the `all` policy (Phase 6)
with `window_timeout_sec <= 0`, emit a one-line warning recommending a timeout, at
end-of-file/section finalization. If Phase 6 isn't landed yet, stub this as a
TODO; `first` needs no warning.

**Verify:** unit-extend `test/trace_config_parse/test_config_parser.c` — assert
`window_timeout` parses into `mode_after.window_timeout_sec`, and that
`window_timeout` set *before* `trace_mode_after` survives (regression for the
gotcha).

## 5. Phase 3 — environment variable (`PINSIGHT_TRACE_WINDOW` + alias)

**[src/trace_config.c:147-171](../src/trace_config.c#L147-L171)** in
`setup_trace_config_env()`:

1. Read the new name first, fall back to the deprecated alias with a warning:

```c
char *win_env = getenv("PINSIGHT_TRACE_WINDOW");
if (!win_env) {
    win_env = getenv("PINSIGHT_TRACE_RATE");          /* deprecated alias */
    if (win_env)
        fprintf(stderr, "PInsight WARNING: PINSIGHT_TRACE_RATE is deprecated; "
                        "use PINSIGHT_TRACE_WINDOW (same grammar)\n");
}
```

2. New grammar `start:max:rate:window_timeout[:mode_after_string]`:

```c
int start=0, max=0, rate=0, win=0;
int count = sscanf(win_env, "%d:%d:%d:%d", &start, &max, &rate, &win);
if (count >= 1) lexgion_default_trace_config->trace_starts_at = start;
if (count >= 2) lexgion_default_trace_config->max_num_traces  = max;
if (count >= 3) lexgion_default_trace_config->tracing_rate    = rate;
if (count >= 4) lexgion_default_trace_config->mode_after.window_timeout_sec = win;
```

3. Advance past **four** colons (was three) before
   `parse_trace_mode_after()`
   ([src/trace_config.c:160-170](../src/trace_config.c#L160-L170)): change the
   `while (colons < 3)` walk to `< 4`.

**⚠ Breaking grammar change** (both names): document in the changelog and update
[PINSIGHT_TRACE_CONFIG_FORMAT.md](PINSIGHT_TRACE_CONFIG_FORMAT.md).

**Verify:** `PINSIGHT_TRACE_WINDOW=0:50:1:30:HIP:MONITORING` sets max=50,
window=30, and HIP mode_after=MONITORING; `PINSIGHT_TRACE_RATE=…` prints the
deprecation warning and parses identically; legacy 3-field `0:50:1` still works
(count<4 → window stays 0).

## 6. Phase 4 — control-thread timer (the core)

All in **[src/pinsight_control_thread.c](../src/pinsight_control_thread.c)**;
file-scope, control-thread-only state (no locking).

**6.1 State + helper.** Near the other file-scope statics
([line ~52](../src/pinsight_control_thread.c#L52)):

```c
static int                 window_timer_armed = 0;
static struct timespec     window_deadline;     /* CLOCK_REALTIME */
static trace_mode_after_t *window_timer_ma = NULL;

static void window_timer_arm_from_default(void) {
    trace_mode_after_t *ma = &lexgion_default_trace_config->mode_after;
    if (ma->window_timeout_sec > 0) {
        clock_gettime(CLOCK_REALTIME, &window_deadline);
        window_deadline.tv_sec += ma->window_timeout_sec;
        window_timer_ma = ma;
        window_timer_armed = 1;
    } else {
        window_timer_armed = 0;
        window_timer_ma = NULL;
    }
}
```

(Single per-process timer drives off the default lexgion's `mode_after`, matching
how `PINSIGHT_TRACE_WINDOW`/`[Lexgion.default]` set it.)

**6.2 Initial arm.** Call `window_timer_arm_from_default()` once just before the
`while (!control_shutdown)` loop
([line 199](../src/pinsight_control_thread.c#L199)) — clock starts at
control-thread start (design decision §7).

**6.3 Wait with deadline.** Replace the unconditional `sem_wait`
([line 203](../src/pinsight_control_thread.c#L203)) with the armed/unarmed
branch, and handle `ETIMEDOUT` by synthesizing a wakeup (design §4):

```c
int rc;
if (window_timer_armed)
    rc = sem_timedwait(&control_sem, &window_deadline);
else
    rc = sem_wait(&control_sem);
if (rc == -1 && errno == EINTR) continue;

int reason;
if (rc == -1 && errno == ETIMEDOUT) {
    trace_mode_after_t *ma = window_timer_ma;
    int expected = 0;
    if (!__atomic_compare_exchange_n(&ma->fired, &expected, 1, 0,
                                     __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)) {
        window_timer_armed = 0;          /* count policy already fired; no-op */
        continue;
    }
    pending_mode_action = ma;
    reason = ma->introspect ? PINSIGHT_WAKEUP_INTROSPECT
                            : PINSIGHT_WAKEUP_MODE_CHANGE;
    window_timer_armed = 0;              /* one-shot; re-armed on cyclic reset */
} else {
    if (control_shutdown) break;
    reason = __atomic_exchange_n(&pending_wakeup_reason, 0, __ATOMIC_SEQ_CST);
}
```

Then the **existing handler block**
([lines 213-282](../src/pinsight_control_thread.c#L213-L282)) runs unchanged for
both the timeout-synthesized and the real wakeups — INTROSPECT, mode-apply, cyclic
reset, `control_apply_all_modes()`.

**6.4 Re-arm points (design §7).**
- After **CONFIG_RELOAD** handling
  ([line 224](../src/pinsight_control_thread.c#L224)): call
  `window_timer_arm_from_default()` so a reload can add/extend/clear the timeout.
- After **cyclic reset** (`cyclic_resume`, `generation++`,
  [line 274](../src/pinsight_control_thread.c#L274)): call
  `window_timer_arm_from_default()` so each new TRACING window is bounded. **This
  re-arm is the correctness guard** that makes the "rely on CAS no-op" choice safe
  (stale deadlines are replaced every cycle).

**6.5 Includes.** Ensure `<time.h>`/`<errno.h>` present (INTROSPECT path already
uses `clock_gettime`/`sem_timedwait`, so likely yes).

**Verify (functional):**
- Static MONITORING/STANDBY: timer disabled, no spurious switch.
- `window_timeout=N`, no count cap, long-running HIP loop → HIP flips to
  MONITORING at ≈N s; activity capture stops (`hipKernelActivity` stops growing).
- `window_timeout` + `max_num_traces`: count fires first → at deadline the CAS
  no-ops (one harmless wakeup, no second switch).
- INTROSPECT + `window_timeout`: with no region capping, the script runs and the
  app pauses at ≈N s; cyclic resume re-arms (window 2 also bounded).
- Shutdown before deadline: clean exit 0, no switch.

## 7. Phase 5 — tests

Extend the existing ROCm harness (`test/rocm/`, not git-ignored):

- **Reuse** `test/rocm/looping_pinsight.hip` (long HIP loop) from the cyclic-mode
  work.
- **New** `test/rocm/window_timeout_test.sh`: run with a config setting
  `window_timeout` (no count cap), assert (a) app exits 0, (b) host kernel
  launches continue but `hipKernelActivity` records stop accruing after the
  deadline (capture tied to TRACING → MONITORING), (c) the deadline lands within a
  tolerance band. Run the app **directly** so `$!` is the app PID (the
  SIGUSR1/`timeout`-wrapper gotcha from cyclic_mode_test).
- **New** `Makefile` target `make window_timeout`.
- **Parser unit test** additions per Phase 2/3 above.

## 8. Phase 6 — `mode_after_trigger` enum (companion, separate)

Scaffolding only here; `all` firing logic is its own design task.

- Add `typedef enum { TRIGGER_FIRST, TRIGGER_ALL, TRIGGER_ANCHOR } mode_after_trigger_t;`
  and a field on the lexgion/`trace_mode_after` config; default `TRIGGER_FIRST`.
- Parse `mode_after_trigger = first | all` in the `[Lexgion*]` dispatch
  (Phase 2 site). **Reject `anchor`** with "not yet implemented" (design §2) — do
  not silently ignore.
- `first` = today's CAS-latch behavior (no logic change). `all` firing
  (per-thread "every seen region capped") is **deferred** to its own design +
  task; until then `all` parses, warns if no `window_timeout`, and behaves as a
  documented stub.

This phase is independent of Phases 0–5 and must not block the `window_timeout`
landing.

## 9. Docs to update at landing

- [PINSIGHT_TRACE_CONFIG_FORMAT.md](PINSIGHT_TRACE_CONFIG_FORMAT.md) — new
  `[Lexgion*] window_timeout` key; `PINSIGHT_TRACE_WINDOW` env + new grammar +
  `PINSIGHT_TRACE_RATE` deprecation.
- `README.md` — env-var example using the new name/grammar.
- Changelog — the breaking `PINSIGHT_TRACE_*` grammar change + alias deprecation.
- `doc/code-memory/` — note the feature once validated (mirror to user memory).

## 10. Risks & rollback

- **Breaking env grammar.** Mitigated by the alias + warning; only affects users
  with a 4-field `PINSIGHT_TRACE_RATE`. Rollback = revert Phase 3 alone.
- **Control-thread `sem_timedwait` correctness.** Mirrors the existing INTROSPECT
  pause path; main residual risk is the cyclic re-arm — covered by Phase 5
  multi-window test. Killswitch: if `window_timeout_sec <= 0` everywhere, the loop
  takes the original `sem_wait` path → behavior identical to today.
- **Activity-tail on switch** (HIP): a few in-flight async records may flush after
  the switch — already the documented/accepted behavior from the TRACING-only
  capture work; not introduced here.
- Each phase is independently revertable; Phases 0–3 are inert until Phase 4 reads
  the field/arms the timer.

## 11. Landing checklist

- [x] Phase 0 rename, `build_hip/` green, INTROSPECT unchanged
- [x] Phase 1 field added, compiles
- [x] Phase 2 file parse + preserve-on-reload gotcha + parser unit test (WT1–WT7)
- [x] Phase 3 env var + alias warning + 4-colon walk + env test
- [x] Phase 4 timer arm/fire/re-arm + synthesized wakeup (+ `window_already_ended`
      cross-config arbiter fix found in testing, see design §10)
- [x] Phase 5 tests — `test/mode_window/` GPU-free end-to-end (all checks pass) +
      `test/rocm/window_timeout_test.sh` + `make window`. **Pending:** the HIP
      script has not yet been run on an MI300A node (needs a flux bank).
- [x] Docs (§9) updated; changelog notes the breaking change
- [x] Phase 6 — `all` **fully implemented** (per-thread gate), `anchor` rejected

**Remaining before calling it fully done:** run `make window` on a Tuolumne
MI300A node for HIP end-to-end confirmation (GPU-free OpenMP path already validated
the same domain-agnostic timer/gate code).
