# 4-Mode Domain Trace Design

## Overview

PInsight introduces a 4-mode domain trace hierarchy that replaces the current 3-mode system (OFF/MONITORING/TRACING). The new design adds **STANDBY** mode between OFF and MONITOR, and makes **OFF permanent** (one-way, irreversible teardown).

---

## Mode Hierarchy

```
OFF (0)  →  STANDBY (1)  →  MONITOR (2)  →  TRACE (3)
 zero      callback-ready    lexgion tracking   full LTTng
permanent   reversible        reversible        reversible
```

### Per-Mode Semantics

| Mode | Callback dispatch | LRU lookup | `count++` | Config lookup | Rate decision | LTTng | Reversible |
|------|------------------|------------|-----------|---------------|---------------|-------|------------|
| **OFF** | ❌ Deregistered/finalized | ❌ | ❌ | ❌ | ❌ | ❌ | ❌ Permanent |
| **STANDBY** | ✅ → immediate return | ❌ | ❌ | ❌ | ❌ | ❌ | ✅ |
| **MONITOR** | ✅ | ✅ | ✅ | ❌ | ❌ | ❌ | ✅ |
| **TRACE** | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |

Each mode adds exactly one layer of cost on top of the previous. The overhead gap between adjacent modes is dominated by a single cost factor:
- **STANDBY → MONITOR**: LRU lookup cost
- **MONITOR → TRACE**: Config resolution + rate decision + LTTng emission

### Callback Pseudocode by Mode

**OFF**: Callback never fires (deregistered at runtime level).

**STANDBY**:
```c
void callback(codeptr_ra, ...) {
    if (domain_mode == STANDBY) return;  // immediate return
    ...
}
```

**MONITOR**:
```c
void callback(codeptr_ra, ...) {
    lexgion = LRU_lookup(codeptr_ra);   // find or create
    lexgion->count++;                    // increment
    return;                              // no config, no rate, no trace_bit
}
```

**TRACE**:
```c
void callback(codeptr_ra, ...) {
    lexgion = LRU_lookup(codeptr_ra);
    lexgion->count++;
    config = resolve_trace_config(lexgion);   // hierarchical lookup with caching
    trace_bit = rate_decision(config, count); // trace_starts_at, tracing_rate, max_num_traces
    if (trace_bit) emit_tracepoint(...);      // LTTng
    check_auto_trigger(config, trace_count);  // INTROSPECT / mode_after
}
```

---

## Mode Transitions

### Allowed Transition Graph

```
STANDBY ↔ MONITOR ↔ TRACE
   ↓         ↓        ↓
  OFF       OFF      OFF   (one-way, permanent)
```

- Any mode can transition to **OFF**, but OFF is **terminal** — no recovery.
- **STANDBY**, **MONITOR**, and **TRACE** are freely interchangeable via SIGUSR1.

### Transition Matrix

| From \ To | OFF | STANDBY | MONITOR | TRACE |
|-----------|-----|---------|---------|-------|
| **OFF** | — | ❌ | ❌ | ❌ |
| **STANDBY** | ✅ | — | ✅ | ✅ |
| **MONITOR** | ✅ | ✅ | — | ✅ |
| **TRACE** | ✅ | ✅ | ✅ | — |

### Per-Domain Transition Mechanisms

#### OpenMP (OMPT)

| Transition | Mechanism |
|-----------|-----------|
| → OFF | `ompt_finalize_tool()` — permanent OMPT shutdown |
| → STANDBY | `ompt_set_callback(event, NULL)` for all events — deregister but tool stays active |
| → MONITOR | `ompt_set_callback(event, monitor_fn)` — register lightweight callbacks (LRU + count) |
| → TRACE | `ompt_set_callback(event, trace_fn)` — register full callbacks |

#### CUDA (CUPTI)

| Transition | Mechanism |
|-----------|-----------|
| → OFF | `cuptiUnsubscribe(subscriber)` — permanent teardown |
| → STANDBY | `cuptiEnableCallback(0, subscriber, domain, cbid)` — subscriber alive, dispatch off |
| → MONITOR | `cuptiEnableCallback(1, ...)` — dispatch on, callback does LRU + count only |
| → TRACE | `cuptiEnableCallback(1, ...)` — dispatch on, full tracing path |

#### MPI (PMPI)

| Transition | Mechanism |
|-----------|-----------|
| → OFF | Set killswitch flag; early return as first statement in wrapper |
| → STANDBY | `PINSIGHT_DOMAIN_ACTIVE` check → early return (same overhead as OFF killswitch) |
| → MONITOR | Wrapper proceeds to LRU lookup + count, skips LTTng |
| → TRACE | Full wrapper path |

> **Note on STANDBY vs OFF for CUDA**: STANDBY keeps the CUPTI subscriber alive but disables callback dispatch. Re-enabling is a single `cuptiEnableCallback(1, ...)` call (~µs). OFF unsubscribes entirely — no recovery. This makes STANDBY's re-activation much faster than re-subscribing from scratch.

---

## Counter Semantics

### Which mode increments which counter?

| Counter | STANDBY | MONITOR | TRACE |
|---------|---------|---------|-------|
| `count` (total observed executions) | ❌ | ✅ | ✅ |
| `trace_count` (actual traces emitted) | ❌ | ❌ | ✅ |

### Rate-control parameter behavior

| Parameter | Checks against | When counted |
|-----------|---------------|-------------|
| `trace_starts_at` | `count` | Advances in MONITOR and TRACE |
| `tracing_rate` | `count % rate` | Advances in MONITOR and TRACE |
| `max_num_traces` | `trace_count` | Only in TRACE |

### Transition scenarios

**Start in STANDBY → switch to TRACE**:
- `count = 0`, `trace_count = 0` — fresh start
- `trace_starts_at = 100` → skip first 100 observed executions after switch
- Counters build up naturally from zero

**Start in MONITOR → switch to TRACE**:
- `count` already advanced during MONITOR (e.g., 5000)
- `trace_count = 0` — never traced
- `trace_starts_at = 100` → since count > 100, tracing starts immediately ✅
- MONITOR served as warm-up

**TRACE → STANDBY → TRACE** (pause in the middle):
- `count` and `trace_count` freeze during STANDBY
- Resume from where they left off when switching back to TRACE
- No counter reset — progress preserved

### Counter reset on INTROSPECT cycle

When `auto_triggered` is reset (via SIGUSR1 config reload after INTROSPECT), `trace_count` should also be reset to 0 for that lexgion. This ensures each introspection cycle starts with a fresh trace budget:

```
TRACE (200 traces) → INTROSPECT → resume → trace_count=0 → TRACE (200 more) → INTROSPECT → ...
```

Without this reset, `trace_count` remains at `max_num_traces` after SIGUSR1 resets `auto_triggered`, causing immediate re-trigger.

---

## Integration with INTROSPECT

### Trigger path

INTROSPECT fires **only from TRACE mode** because:
- Only TRACE performs config lookup and auto-trigger checks
- MONITOR and STANDBY never examine `max_num_traces` or `trace_mode_after`

### Resume modes (now richer with STANDBY)

| Config | Resume to | Behavior |
|--------|-----------|----------|
| `INTROSPECT:60:script.sh:TRACE` | TRACE | **Cyclic**: trace → introspect → trace → introspect → ... |
| `INTROSPECT:60:script.sh:MONITOR` | MONITOR | **One-shot**: trace → introspect → count-only (no more traces) |
| `INTROSPECT:60:script.sh:STANDBY` | STANDBY | **One-shot, minimal overhead**: trace → introspect → near-zero overhead. Script or user sends SIGUSR1 to re-activate. |
| `INTROSPECT:60:script.sh:OFF` | OFF | **One-shot, permanent**: trace → introspect → teardown. No recovery. |

### STANDBY as resume mode

STANDBY + INTROSPECT enables **script-driven tracing lifecycle**:

```
App start: STANDBY (near-zero overhead)
    ↓ (SIGUSR1 from operator or script)
Phase 1: MONITOR (warm up LRU + counters)
    ↓ (SIGUSR1)
Phase 2: TRACE (trace_count accumulates from 0)
    ↓ (trace_count hits max_num_traces)
INTROSPECT: lttng rotate → script → block
    ↓ (timeout or SIGUSR1)
Resume: STANDBY (near-zero overhead, app runs fast)
    ↓ (script analyzes traces, decides when to re-trace)
Phase 3: TRACE (script sends SIGUSR1, trace_count reset to 0)
    ...
```

The analysis script has full control over the tracing lifecycle, parking the app at near-zero overhead between tracing windows.

### Cyclic INTROSPECT counter behavior

For `resume_mode = TRACE` (cyclic):

| Step | `count` | `trace_count` | `auto_triggered` | Action |
|------|---------|--------------|-------------------|--------|
| Tracing | Incrementing | Incrementing | false | Normal tracing |
| `trace_count` hits `max_num_traces` | — | 200 | → true | Fire INTROSPECT |
| During INTROSPECT | Frozen | 200 | true | Blocked (sigsuspend) |
| Resume to TRACE | — | **→ 0** | **→ false** | Fresh cycle begins |
| Tracing resumes | Incrementing | Incrementing | false | Normal tracing |

---

## Configuration

### Enum Extension

```c
typedef enum {
    PINSIGHT_DOMAIN_OFF = 0,        // Permanent teardown
    PINSIGHT_DOMAIN_STANDBY = 1,    // Callback-ready, no lexgion work
    PINSIGHT_DOMAIN_MONITORING = 2, // LRU lookup + count, no LTTng
    PINSIGHT_DOMAIN_TRACING = 3     // Full tracing
} pinsight_domain_mode_t;
```

### Updated macros

```c
// True for MONITORING and TRACING (modes that do lexgion work)
#define PINSIGHT_DOMAIN_ACTIVE(mode) ((mode) >= PINSIGHT_DOMAIN_MONITORING)

// True only for TRACING
#define PINSIGHT_SHOULD_TRACE(mode) ((mode) == PINSIGHT_DOMAIN_TRACING)

// True for STANDBY, MONITORING, and TRACING (callbacks registered)
#define PINSIGHT_DOMAIN_ALIVE(mode) ((mode) >= PINSIGHT_DOMAIN_STANDBY)
```

### Environment variable

```bash
PINSIGHT_TRACE_<DOMAIN>=<MODE>
```

| Mode | Accepted Values |
|------|----------------|
| OFF | `OFF`, `FALSE`, `0` |
| STANDBY | `STANDBY` |
| MONITORING | `MONITORING`, `MONITOR` |
| TRACING | `ON`, `TRACING`, `TRUE`, `1` (default) |

### Config file

```ini
[OpenMP.global]
    trace_mode = STANDBY

[CUDA.global]
    trace_mode = TRACE

[MPI.global]
    trace_mode = OFF
```

### `trace_mode_after` with 4 modes

All four modes are valid as `trace_mode_after` values:

```ini
trace_mode_after = STANDBY                        # switch to STANDBY
trace_mode_after = OpenMP:STANDBY, MPI:OFF        # per-domain
trace_mode_after = INTROSPECT:60:script.sh:STANDBY # introspect then STANDBY
```

---

## Design Rationale

### Why split OFF into OFF + STANDBY?

The current 3-mode OFF tries to do two things:
1. **Zero overhead** — callbacks deregistered
2. **Recoverable** — can re-register via SIGUSR1

These goals conflict. True zero overhead for CUDA requires `cuptiUnsubscribe()`, which is permanent. The current OFF uses `cuptiEnableCallback(0)` instead, which is recoverable but still pays the subscriber overhead.

The 4-mode design resolves this:
- **OFF**: True permanent teardown. `ompt_finalize_tool()`, `cuptiUnsubscribe()`. Zero overhead guaranteed.
- **STANDBY**: Recoverable near-zero overhead. Callbacks deregistered but tool infrastructure alive. Fast re-activation.

### Why MONITOR keeps counting

Since MONITOR already pays for the LRU lookup (the dominant cost), adding `count++` is a single memory increment — negligible. The counter enables:
- `trace_starts_at` warm-up during MONITOR → immediate tracing on switch to TRACE
- Continuity of rate-control state across mode transitions

### Why MONITOR skips config lookup

Config lookup resolves "should I trace this execution?" — a question only TRACE mode asks. MONITOR's answer is always "no", so the lookup would be wasted work.
