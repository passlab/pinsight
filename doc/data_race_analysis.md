# Data Race Analysis: Global Trace Config Objects

## Global Objects Under Analysis

| Object | Declared in | Type |
|---|---|---|
| `domain_default_trace_config[]` | `trace_config.c` | `domain_trace_config_t[MAX_NUM_DOMAINS]` |
| `lexgion_default_trace_config` | `trace_config.c` | `lexgion_trace_config_t *` (points into `all_lexgion_trace_config[]`) |
| `lexgion_domain_default_trace_config` | `trace_config.c` | `lexgion_trace_config_t *` |
| `lexgion_address_trace_config` | `trace_config.c` | `lexgion_trace_config_t *` |
| `trace_config_change_counter` | `trace_config.c` | `unsigned int` |
| `mode_change_requested` | `pinsight.c` | `volatile sig_atomic_t` |
| `config_reload_requested` | `trace_config.c` | `volatile sig_atomic_t` |

---

## Write Paths

### W1: `pinsight_fire_mode_triggers()` — called from any thread

Called via → `lexgion_post_trace_update()` → `on_ompt_callback_parallel_end()` / `callback_work` / `callback_masked`

**Writes:**
- `domain_default_trace_config[d].mode` (the domain mode enum)
- `domain_default_trace_config[d].mode_change_fired` (the latch flag)
- `mode_change_requested` (volatile sig_atomic_t)

**Can be concurrent?** Yes — multiple threads can reach `parallel_end` simultaneously when nested parallelism or multiple independent parallel regions exist. Each thread's lexgion may independently reach `max_num_traces`.

### W2: `pinsight_load_trace_config()` — called from `parallel_begin` (master only)

Called via → `on_ompt_callback_parallel_begin()` guarded by atomic exchange of `config_reload_requested`

**Writes:**
- `lexgion_domain_default_trace_config[i].codeptr` (reset to NULL)
- All fields of `lexgion_default_trace_config`, `lexgion_domain_default_trace_config`, `lexgion_address_trace_config` (via `parse_trace_config_file`)
- `trace_config_change_counter++`

**Can be concurrent?** This runs in `parallel_begin`, which is a **pre-fork sequential point** — only the encountering thread executes it before the team is created. So in a given parallel region, this is safe. However, worker threads from a **previously active nested region** could still be reading config concurrently.

### W3: Signal handler `pinsight_sigusr1_handler()` — async

**Writes:** Only `config_reload_requested = 1` (sig_atomic_t, async-signal-safe).

---

## Read Paths

### R1: `PINSIGHT_DOMAIN_ACTIVE(mode)` / `PINSIGHT_SHOULD_TRACE(domain)` — every callback, every thread

Reads `domain_default_trace_config[domain].mode`. Called at the top of `parallel_begin`, `parallel_end`, `implicit_task`, `work`, `masked`, `sync_region`, etc.

### R2: `lexgion_set_top_trace_bit_domain_event()` — every begin callback

Reads `trace_config_change_counter`, and dereferences `lexgion_address_trace_config`, `lexgion_domain_default_trace_config`, `lexgion_default_trace_config` to resolve and cache `lgp->trace_config`.

### R3: `lexgion_post_trace_update()` → reads `lgp->trace_config->max_num_traces`, `lgp->trace_config->mode_after[]`

---

## Race Conditions

### Race 1: `pinsight_fire_mode_triggers()` concurrent calls — ⚠️ LOW severity

**Scenario:** Thread A and Thread B both reach `parallel_end` at the same time, and both their lexgions have `trace_counter >= max_num_traces`.

**Race on:**
- `mode_change_fired` check-then-set (lines 20–22 of `pinsight.c`)
- `mode` write (line 21)

**Impact:** Both threads may read `mode_change_fired == 0`, both enter the `if` body, and both write `mode = new_mode` and `mode_change_fired = 1`. Since both write the **same values** (they read from the same `tc->mode_after[d]`), this is a **benign race** — the result is correct regardless of interleaving. The only observable effect is a duplicate `fprintf` to stderr, which is not harmful.

**Severity: LOW** — Functionally benign. The duplicate log message is cosmetic.

---

### Race 2: Config reload (W2) vs. callback reads (R1, R2) — ⚠️ MEDIUM severity

**Scenario:** `parallel_begin` on Thread 0 calls `pinsight_load_trace_config()` while worker threads from a previous/nested parallel region are executing callbacks that read the same config objects.

**Race on:**
- `lexgion_domain_default_trace_config[i]` — reload zeroes `codeptr`, then overwrites entire struct. A reader in R2 could see partially-written state (e.g., `codeptr != NULL` but `trace_starts_at` from old config, `tracing_rate` from new config).
- `trace_config_change_counter` — increment is non-atomic. A reader could miss the update or see a torn value (very unlikely on x86 for a 32-bit int but technically UB per C11).

**Impact:** A thread resolving its config via `lexgion_set_top_trace_bit_domain_event()` could:
1. Cache a pointer to a config object that is being mutated → reads stale/mixed rate values
2. See old `trace_config_change_counter` and skip re-resolution for one iteration

**Severity: MEDIUM** — The practical impact is small because:
- Config reloads are rare (manual SIGUSR1 only)
- The race window is narrow (only during the brief config file parse)
- Worst case: one region uses stale rate values for a few invocations until `trace_config_change_counter` propagates
- No crash risk — the config objects are never freed/reallocated, only mutated in-place

**Possible mitigation:** Use a sequence lock or read-copy-update (RCU) pattern. A lighter-weight approach: double-buffer the config (swap a pointer atomically after fully populating the new config).

---

### Race 3: `mode` write (W1) vs. `mode` read (R1) — ✅ BENIGN

**Scenario:** `pinsight_fire_mode_triggers()` writes `domain_default_trace_config[d].mode = new_mode` while another thread reads it via `PINSIGHT_DOMAIN_ACTIVE()` or `PINSIGHT_SHOULD_TRACE()`.

**Impact:** The reader sees either the old mode or the new mode. Both are valid enum values. There is no intermediate/torn state because `mode` is an int-sized enum (written atomically on all practical architectures). The reader simply makes a tracing decision based on whichever mode it observes.

**Severity: BENIGN** — This is the intended "eventually consistent" design. The mode change takes effect "soon" across all threads, and deferred callback re-registration happens at the next `parallel_begin`.

---

### Race 4: `mode_change_requested` flag — ✅ SAFE

**Writers:** `pinsight_fire_mode_triggers()` sets it to `1`.
**Reader:** `parallel_begin` uses `__atomic_exchange_n(..., __ATOMIC_SEQ_CST)`.

The atomic exchange ensures the flag is consumed exactly once. Multiple concurrent writers setting it to `1` are harmless (idempotent). **No race issue.**

---

### Race 5: `config_reload_requested` flag — ✅ SAFE

**Writer:** Signal handler (sets to `1`, sig_atomic_t is async-signal-safe).
**Reader:** `parallel_begin` uses `__atomic_exchange_n(..., __ATOMIC_SEQ_CST)`.

Same pattern as Race 4. **No race issue.**

---

## Summary

| Race | Objects | Severity | Impact |
|---|---|---|---|
| **#1** — Concurrent fire_mode_triggers | `.mode`, `.mode_change_fired` | **LOW** | Duplicate stderr log, same values written |
| **#2** — Config reload vs. reads | `lexgion_*_trace_config`, `trace_config_change_counter` | **MEDIUM** | Briefly stale/mixed rate values during SIGUSR1 reload |
| **#3** — Mode write vs. read | `.mode` | **BENIGN** | Eventually consistent by design |
| **#4** — `mode_change_requested` | flag | **SAFE** | Atomic exchange, idempotent writers |
| **#5** — `config_reload_requested` | flag | **SAFE** | sig_atomic_t + atomic exchange |

## Recommendations

1. **Race #1** — If the duplicate `fprintf` bothers you, use `__atomic_exchange_n(&mode_change_fired, 1, ...)` as a test-and-set. Otherwise, leave as-is; it's functionally correct.

2. **Race #2** — This is the only race with real (though minor) semantic consequences. Options by increasing complexity:
   - **Accept it** — reloads are rare, and the worst case is a brief period of mixed config values. Already mitigated by `trace_config_change_counter` re-resolution.
   - **Double-buffer** — populate a new config in a shadow copy, then atomically swap the pointer. Threads still using the old config finish safely.
   - **Sequence lock** — wrap the reload in a seqlock; readers retry if they detect a concurrent write. Low overhead for the read-mostly pattern.
