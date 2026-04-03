# 4-Mode Domain Trace: Implementation Plan

Based on the design in [four_mode_trace_design.md](four_mode_trace_design.md).

---

## Phase 1: Enum and Macros

### Files: `src/trace_config.h`

1. Add `PINSIGHT_DOMAIN_STANDBY = 1` to `pinsight_domain_mode_t` enum:
   ```diff
   typedef enum {
       PINSIGHT_DOMAIN_OFF = 0,
   +   PINSIGHT_DOMAIN_STANDBY = 1,
   -   PINSIGHT_DOMAIN_MONITORING = 1,
   -   PINSIGHT_DOMAIN_TRACING = 2
   +   PINSIGHT_DOMAIN_MONITORING = 2,
   +   PINSIGHT_DOMAIN_TRACING = 3
   } pinsight_domain_mode_t;
   ```

2. Update macros:
   ```diff
   -#define PINSIGHT_DOMAIN_ACTIVE(mode) ((mode) >= PINSIGHT_DOMAIN_MONITORING)
   +#define PINSIGHT_DOMAIN_ACTIVE(mode) ((mode) >= PINSIGHT_DOMAIN_MONITORING)
   +#define PINSIGHT_DOMAIN_ALIVE(mode)  ((mode) >= PINSIGHT_DOMAIN_STANDBY)
    #define PINSIGHT_SHOULD_TRACE(domain) \
        (domain_default_trace_config[domain].mode == PINSIGHT_DOMAIN_TRACING)
   ```

> [!IMPORTANT]
> `PINSIGHT_DOMAIN_ACTIVE` semantics **do not change** (still means MONITORING or TRACING). Existing code using this macro works as-is. The new `PINSIGHT_DOMAIN_ALIVE` macro is used only for new STANDBY-aware logic.

---

## Phase 2: Mode Parsing

### Files: `src/trace_config.c`, `src/trace_config_parse.c`

1. **Environment variable parsing** — add `STANDBY` to accepted values:
   ```c
   // In parse_domain_mode() or equivalent
   if (strcasecmp(val, "STANDBY") == 0)
       return PINSIGHT_DOMAIN_STANDBY;
   ```

2. **Config file parsing** — `trace_mode` key in `[Domain.global]` sections already accepts mode strings. Add `"STANDBY"` to the parser.

3. **`trace_mode_after` parsing** — `parse_trace_mode_after()` already parses mode strings for the resume mode field. Add `"STANDBY"` as a valid resume mode.

4. **Pretty-printing** — update serialization to output `"STANDBY"` for the new enum value.

---

## Phase 3: OFF Mode — Make Permanent

### Files: `src/pinsight.c`, `src/ompt_callback.c`, `src/cupti_callback.c`

This is the breaking behavioral change: OFF becomes **irreversible**.

1. **OpenMP**: When mode switches to OFF, call `ompt_finalize_tool()` instead of individual `ompt_set_callback(event, NULL)`. After finalization, the OMPT tool is permanently shut down.

2. **CUDA**: When mode switches to OFF, call `cuptiUnsubscribe(subscriber)` to permanently tear down the CUPTI subscriber. The current `cuptiEnableCallback(0, ...)` approach moves to STANDBY.

3. **MPI**: No change needed — the killswitch early-return already works. For permanent OFF, set a `mpi_permanently_off` flag that prevents any future re-activation.

4. **Disable SIGUSR1 re-activation for OFF domains**: In the SIGUSR1 config reload handler, skip any domain whose mode is OFF. The current `pinsight_wakeup_from_off_openmp()` function is no longer needed — remove it and its call site.

> [!WARNING]
> This changes existing behavior. Currently, OFF → TRACING via SIGUSR1 is supported. After this change, switching to OFF is permanent. Users who want reversible zero-overhead should use STANDBY instead.

---

## Phase 4: STANDBY Mode — New Implementation

### Files: `src/pinsight.c`, `src/ompt_callback.c`, `src/cupti_callback.c`, `src/pmpi_mpi.c`

STANDBY inherits the current OFF's **recoverable** behavior:

1. **OpenMP STANDBY**: `ompt_set_callback(event, NULL)` for all domain events. Tool remains registered with OMPT. Switch to MONITOR/TRACE: `ompt_set_callback(event, fn)`.

2. **CUDA STANDBY**: `cuptiEnableCallback(0, subscriber, domain, cbid)` to disable dispatch. Subscriber alive. Switch to MONITOR/TRACE: `cuptiEnableCallback(1, ...)`.

3. **MPI STANDBY**: Early-return after `PINSIGHT_DOMAIN_ALIVE(mode)` check. Functionally identical to current OFF behavior for MPI wrappers.

4. **SIGUSR1 transitions from STANDBY**: Reuse existing `pinsight_register_openmp_callbacks()` and `pinsight_sync_cuda_callbacks()` which already handle re-registration. The current "wakeup from OFF" logic becomes "activate from STANDBY".

---

## Phase 5: MONITOR Mode — Simplify to LRU + Count

### Files: `src/ompt_callback.c`, `src/cupti_callback.c`, `src/pmpi_mpi.c`

Currently MONITOR does full lexgion bookkeeping including config lookup and rate decisions. Simplify:

1. **In each callback**: after the domain mode check, add a MONITOR fast-path:
   ```c
   if (domain_mode == PINSIGHT_DOMAIN_MONITORING) {
       lexgion = find_or_create_lexgion(codeptr_ra);
       lexgion->count++;
       return;  // skip config lookup, rate decision, LTTng
   }
   // ... full TRACE path below
   ```

2. **Verify**: lexgion push/pop stack operations are only needed for TRACE mode (for nesting). MONITOR can skip them since it doesn't need trace context.

3. **Test**: Benchmark MONITOR overhead before/after to confirm the improvement.

---

## Phase 6: Counter Reset on INTROSPECT Cycle

### Files: `src/pinsight.c`

When `auto_triggered` is reset (via SIGUSR1 config reload), also reset `trace_count` for all lexgions that had `auto_triggered = true`:

```c
// In pinsight_load_trace_config() or the SIGUSR1 handler
for each lexgion {
    if (lexgion->auto_triggered) {
        lexgion->trace_count = 0;
        lexgion->auto_triggered = false;
    }
}
```

This ensures cyclic INTROSPECT works correctly: each cycle starts with a fresh trace budget.

---

## Phase 7: Tests

### Files: `test/trace_config_parse/test_config_parser.c`

1. **Parsing tests**: Add test cases for `STANDBY` in:
   - Environment variable: `PINSIGHT_TRACE_OPENMP=STANDBY`
   - Config file: `[OpenMP.global] trace_mode = STANDBY`
   - `trace_mode_after = STANDBY`
   - `trace_mode_after = INTROSPECT:60:script.sh:STANDBY`

2. **Mode transition tests**: Add STANDBY to the bidirectional mode switch test:
   - STANDBY → MONITOR → TRACE → STANDBY → TRACE → OFF

3. **Counter behavior tests**: Verify counters don't advance during STANDBY but do advance during MONITOR.

---

## Phase 8: Documentation

### Files: `doc/domain_trace_modes.md`, `README.md`

1. Update mode table from 3 to 4 modes
2. Update environment variable accepted values
3. Update config file examples
4. Update `trace_mode_after` documentation with STANDBY as resume mode
5. Document the OFF permanence change

---

## Implementation Order

| Priority | Phase | Risk | Effort |
|----------|-------|------|--------|
| 1 | Phase 1: Enum + macros | Low | Small |
| 2 | Phase 2: Mode parsing | Low | Small |
| 3 | Phase 4: STANDBY implementation | Medium | Medium — moves current OFF logic |
| 4 | Phase 3: OFF permanent | Medium | Medium — behavioral change |
| 5 | Phase 5: MONITOR simplification | Low | Small — mostly removing code |
| 6 | Phase 6: Counter reset | Low | Small |
| 7 | Phase 7: Tests | Low | Medium |
| 8 | Phase 8: Documentation | Low | Small |

> [!IMPORTANT]
> Phase 3 (OFF permanent) and Phase 4 (STANDBY) should be implemented together — STANDBY takes over the current recoverable-OFF behavior before OFF becomes permanent.

---

## Verification Plan

### Build verification
```bash
cd build && cmake .. && make -j48
./test_config_parser    # all existing + new tests pass
```

### Functional tests
1. **STANDBY → TRACE via SIGUSR1**: start in STANDBY, send SIGUSR1 with TRACE config, verify trace output
2. **OFF permanence**: start in TRACE, switch to OFF, verify SIGUSR1 cannot re-activate
3. **MONITOR counter**: run in MONITOR, switch to TRACE, verify `trace_starts_at` is already advanced
4. **INTROSPECT + STANDBY resume**: verify INTROSPECT can resume to STANDBY and re-activate via SIGUSR1
5. **Counter reset on cyclic INTROSPECT**: verify `trace_count` resets after INTROSPECT with TRACE resume

### Benchmark
- Rerun Jacobi 512×512 benchmark with all 4 modes
- Expected: STANDBY ≈ current OFF (< 3%), MONITOR slightly lower than current MONITORING
