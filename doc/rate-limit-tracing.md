# Rate-Limit Tracing

## What is Rate-Limit Tracing?

Rate-limit tracing is a PInsight feature that constrains the volume of trace data collected during program execution. Instead of tracing **every** invocation of a region (which can generate gigabytes of data), rate-limit tracing allows users to specify:

- **`trace_starts_at`**: Skip the first N invocations before tracing begins.
- **`max_num_traces`**: Stop tracing after collecting N traces.
- **`tracing_rate`**: Trace every Kth invocation (sampling).

Together, these three parameters form the **rate-limit triple** `(trace_starts_at, max_num_traces, tracing_rate)`.

## Motivation and Benefits

1. **Reduced data volume**: Full tracing of every OpenMP region in a real application (e.g., LULESH with 6 threads) can produce 1.5 GB+ of trace data per run. Rate-limit tracing bounds the output to kilobytes or low megabytes.

2. **Lower overhead**: Writing trace data to LTTng buffers has measurable cost (12–42% overhead with full tracing). Rate-limited tracing with a post-trace mode switch to OFF or MONITORING reduces ongoing overhead to near-zero after the tracing window closes.

3. **Representative sampling**: Many parallel applications exhibit steady-state behavior. Tracing the first N iterations is often sufficient to characterize performance. `trace_starts_at` can skip warm-up iterations, and `tracing_rate` enables periodic sampling over longer runs.

4. **Auto-triggered mode switch**: When `max_num_traces` is reached, PInsight can automatically switch the tracing mode (e.g., TRACING → OFF or TRACING → MONITORING) via the `trace_mode_after` directive, eliminating all subsequent tracing overhead.

## User Configuration

Rate-limit tracing is configured in the PInsight trace config file (e.g., `OpenMP_trace_config.txt`) at two levels:

### Domain-level (default for all regions)

```ini
[OpenMP]
trace_mode = TRACING
trace_starts_at = 0
max_num_traces = 100
tracing_rate = 1
trace_mode_after = OFF    # auto-switch when max_num_traces reached
```

This applies rate-limiting to all regions in the OpenMP domain by default. Only the **topmost enclosing region** (e.g., a parallel region) enforces these limits.

### Address-level (specific region)

```ini
[0x401234]
trace_starts_at = 10
max_num_traces = 50
tracing_rate = 5
trace_mode_after = MONITORING
```

This applies rate-limiting to a specific code region identified by its address (`codeptr_ra`). This is required for nested/inner regions (e.g., a specific work loop) to have independent rate-limiting.

## Regions That Can Be Rate-Limit Traced (OpenMP)

### Enclosing regions (topmost — rate-limited by default)

These are the outermost regions that can be independently rate-limit traced using the domain-level config:

| Region | Callback | Implementation Status |
|--------|----------|----------------------|
| **parallel** | `parallel_begin/end` | ✅ Implemented |
| **teams** | `parallel_begin/end` (flag-based) | ✅ Implemented (shares parallel callback) |

### Nested regions (rate-limited only with address-specific config)

These regions are nested inside parallel/teams. By default, they **piggyback** on the enclosing region's tracing decision. To independently rate-limit them, the user must provide an address-specific config entry (`[0xADDRESS]` section):

| Region | Callback | Implementation Status |
|--------|----------|----------------------|
| **loop** | `work_begin/end` (wstype=loop) | ✅ Implemented |
| **sections** | `work_begin/end` (wstype=sections) | ✅ Implemented |
| **single** | `work_begin/end` (wstype=single) | ✅ Implemented |
| **distribute** | `work_begin/end` (wstype=distribute) | ✅ Implemented |
| **taskloop** | `work_begin/end` (wstype=taskloop) | ✅ Implemented |
| **masked** | `masked_begin/end` | ✅ Implemented |
| **explicit task** | `task_create/schedule` | ❌ Not yet implemented |
| **taskgroup** | `sync_region (taskgroup)` | ❌ Not yet implemented |
| **target** | `target_begin/end` | ❌ Not yet implemented |

### Associated regions (never independently rate-limited)

| Region | Callback | Rationale |
|--------|----------|-----------|
| **implicit_task** | `implicit_task_begin/end` | Associated with the enclosing parallel region. The implicit_task lexgion exists mainly so non-master threads can access the enclosing task record. Rate-limiting is enforced by the master thread at `parallel_end`. |
| **sync_region (barrier)** | `sync_region_begin/end` | Inherits tracing decision from the enclosing work or parallel region. |

## Runtime Rate-Limit Checking

### How it works

Each lexgion (region instance identified by code address) maintains a `trace_counter` that tracks how many times it has been traced. The function `lexgion_post_trace_update()` increments this counter and checks if `max_num_traces` has been reached. When reached, it fires auto-trigger mode changes (e.g., switch to OFF mode).

### Per-region enforcement rules

#### Parallel / teams regions

Rate-limiting is enforced by the **master thread** at `parallel_end`. The check uses two conditions:

1. **Is topmost?** — Walk the lexgion stack parent chain. If no enclosing `parallel_begin` lexgion exists, this is the outermost parallel region → always rate-limited using the domain default config.
2. **Has address-specific config?** — Check `lgp->trace_config->codeptr == lgp->codeptr_ra`. If true, the user explicitly configured rate-limiting for this specific parallel region → rate-limited independently (even if nested).

```
if (lexgion_has_address_specific_config(lgp) || is_topmost_parallel) {
    lexgion_post_trace_update(lgp);
}
```

With nested parallel (parallel inside parallel), only the **outermost** parallel triggers rate-limiting by default. The inner parallel is rate-limited only if it has an address-specific config.

#### Work regions (loop, sections, single, distribute, taskloop, masked)

At `work_begin`, PInsight checks `retrieve_lexgion_trace_config(codeptr_ra)`:
- **Address-specific config exists**: Creates its own lexgion with independent `trace_counter` and rate-limiting.
- **No address-specific config**: Piggybacks on the enclosing implicit_task lexgion. No independent rate-limiting — the enclosing parallel region handles it.

At `work_end`, the decision is reflected by checking the stack top type:
```
if (work_has_own_lexgion) {    // top->lgp->type == ompt_callback_work
    lexgion_post_trace_update(lgp);
}
```

The same pattern applies to **masked** regions.

#### Implicit task

Never independently rate-limited. The implicit_task lexgion exists for non-master threads to access the enclosing task record. Rate-limiting for the parallel region is handled by the master thread at `parallel_end`.

#### Explicit task (not yet implemented)

Task regions can nest (task inside task). When implemented, the rate-limiting check will use the same combined condition as parallel regions:
- Rate-limit if address-specific config **OR** topmost explicit task (no enclosing explicit task on the stack).

#### Taskgroup (not yet implemented)

Taskgroup regions are sync regions that group tasks. When implemented, they would follow the same pattern as work regions — rate-limited only with address-specific config.

#### Target (not yet implemented)

Target regions offload computation to accelerators. When implemented, they would be treated as top-level regions (similar to parallel) for rate-limiting purposes, enforcing the domain default by default.

## Example Configuration

### Trace first 100 iterations, then switch to OFF

```ini
[OpenMP]
trace_mode = TRACING
trace_starts_at = 0
max_num_traces = 100
tracing_rate = 1
trace_mode_after = OFF
```

This traces every invocation of the topmost parallel region until 100 traces are collected, then deregisters all OMPT callbacks (OFF mode) for near-zero overhead.

### Sample every 10th iteration, no limit

```ini
[OpenMP]
trace_mode = TRACING
trace_starts_at = 0
max_num_traces = -1
tracing_rate = 10
```

This traces every 10th invocation indefinitely, reducing data volume by ~10×.

### Rate-limit a specific hot loop independently

```ini
[OpenMP]
trace_mode = TRACING

[0x401234]
trace_starts_at = 50
max_num_traces = 20
tracing_rate = 1
trace_mode_after = MONITORING
```

This traces the loop at address `0x401234` starting from its 50th invocation, collecting 20 traces, then switches to MONITORING. Other regions continue using the domain default (full TRACING with no rate limit).
