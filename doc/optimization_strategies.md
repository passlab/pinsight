# PInsight Overhead Optimization Strategies

**Context**: LULESH benchmarks show MONITORING overhead of 4-16% (1T-6T), nearly identical to TRACING overhead. The bottleneck is callback dispatch + lexgion bookkeeping, not LTTng tracepoint emission.

---

## Strategy 1: Selective Callback Deregistration in MONITORING Mode ✅ Implemented

Deregister non-essential callbacks when mode is MONITORING. Keep only:
- `thread_begin/end` — lifecycle management
- `parallel_begin/end` — region counting, lexgion stack

Deregister: `sync_region`, `sync_region_wait`, `work`, `masked`, `implicit_task`, `mutex_released`, `task_create`, `task_schedule`, `dependences`, `target*`, `device*`, `control_tool`.

**Result (LULESH s=30, 6T)**: Overhead reduced from **15.5% → 11.1%** (~28% reduction) by eliminating sync/work/barrier callbacks.

---

## Strategy 2: Parent-Pointer Stack + Deregister `implicit_task` ✅ Implemented

### 2a: Parent-Pointer Linked Stack

Replaced the fixed-size array stack (`lexgion_stack[16]` + `stack_top` index) with a parent-pointer linked stack. Added `parent` pointer to `lexgion_record_t`:

```c
typedef struct lexgion_record_t {
  lexgion_t *lgp;
  unsigned int record_id;
  struct lexgion_record_t *parent;  // link to enclosing record
} lexgion_record_t;
```

Records are still stored in the pre-allocated array (cache locality), but traversal uses the parent chain. This enables:
- `parallel_end` to use `parent_task->ptr` + `record->parent` instead of `top_lexgion_type()` stack walks
- Unlimited nesting depth for future callback extensions
- Natural integration with OMPT data pointers (`parallel_data->ptr`, `task_data->ptr`)

### 2b: Defer SIGUSR1 Config Reload to `parallel_end`

Moved `config_reload_requested` handling from `lexgion_set_top_trace_bit_domain_event` (racy mid-parallel) to `parallel_end` (safe sequential post-join), alongside `mode_change_requested`. Eliminates data races during config reload and enables safe callback re-registration for MONITORING↔TRACING transitions.

### 2c: Deregister `implicit_task` in MONITORING

With SIGUSR1 reload deferred to `parallel_end`, bidirectional MONITORING↔TRACING switching is safe at region boundaries. This enables deregistering `implicit_task` in MONITORING:

- `parallel_end` skips context restoration (`parent_task->ptr`, enclosing records) in MONITORING since `implicit_task` may not have set `task_data->ptr`
- Workers receive NO callbacks in MONITORING (only `thread_begin/end` for lifecycle)
- Eliminates N×R callbacks per iteration (6×44 = 264 at 6T for LULESH)

**Result (LULESH s=30, 6T)**: Overhead reduced from **11.1% → 4.9%** (56% further reduction).

### Combined Results

| Config | 1T | 2T | 4T | 6T |
|--------|------|------|------|------|
| MONITORING (original) | +3.6% | +7.1% | +9.5% | **+15.5%** |
| opt #1 (deregister sync/work) | +2.6% | +6.3% | +9.3% | **+11.1%** |
| **opt #2 (+ deregister implicit_task)** | **+2.3%** | **+3.9%** | **+3.6%** | **+4.9%** |
| RATE → OFF (reference) | +2.1% | +2.2% | +1.6% | +12.5% |

**Key finding**: opt #2 MONITORING now outperforms RATE→OFF at higher thread counts (4.9% vs 12.5% at 6T). The remaining ~5% overhead comes from just 44 `parallel_begin/end` + `lexgion_begin/end` calls per iteration on the master thread.

---

## ~~Strategy 3: Hash Map for `find_lexgion`~~ ❌ Reverted — Cache Regression

**Idea**: Replace O(n) linear scan in `find_lexgion` with an open-addressing hash table keyed on `(codeptr_ra, type)` for O(1) lookup.

**Implementation**: Added 1024-slot hash table (`lexgion_hash_entry_t[1024]`) to `pinsight_thread_data`, using linear probing. Hash key combined `codeptr_ra >> 4` with type.

**Result (LULESH s=30)**: **Regression across all thread counts in TRACING mode.**

| Mode | 1T | 2T | 4T | 6T |
|------|------|------|------|------|
| TRACING (before hash) FOM | 1018 | 1485 | 2196 | 2560 |
| TRACING (with hash) FOM | 937 | 1340 | 2033 | 2420 |
| **Change** | **−8%** | **−10%** | **−7%** | **−5%** |
| MONITORING (before hash) FOM | 1028 | 1510 | 2390 | 2900 |
| MONITORING (with hash) FOM | 1025 | 1503 | 2411 | 2930 |
| **Change** | ~0% | ~0% | ~0% | ~0% |

**Root cause**: The 1024-entry hash table adds ~24KB to thread-local storage per thread. This displaces useful data from L1 cache (typically 32-48KB), increasing cache misses throughout the application. LULESH only has 44 distinct parallel regions, so the existing MRU + linear scan (at most 44 comparisons of pointer+int) is already fast enough — the algorithmic improvement of O(1) vs O(44) is dwarfed by the cache penalty.

MONITORING is barely affected because `find_lexgion` is only called from `parallel_begin` (master thread, 44×/iteration), not from the eliminated callbacks.

**Conclusion**: Reverted. The hash table would only help applications with hundreds or thousands of distinct lexgions where the linear scan becomes a real bottleneck. For a future attempt, a much smaller table (64-128 entries, ~1.5-3KB) might avoid the cache pressure while still providing O(1) lookup.

## ~~Strategy 4: Per-Thread Counter Batching~~ ❌ Not Applicable
`lgp->counter` is already thread-local — each thread maintains its own `pinsight_thread_data.lexgions[]` array, so `lgp->counter++` has zero contention. Additionally, counters are per-region and drive per-region trace rate decisions (`tracing_rate`, `max_num_traces`), so they cannot be batched or merged across different lexgions with different trace configs.

---

## Overhead Breakdown (LULESH, 6T)

| Component | Status | Impact |
|-----------|--------|--------|
| Sync/work/barrier callbacks | ✅ Eliminated in MONITORING | ~4% |
| `implicit_task` callbacks (6×44/iter) | ✅ Eliminated in MONITORING | ~6% |
| `parallel_begin/end` (44/iter, master only) | Active | ~5% |
| `lexgion_begin/end` (hash + stack) | Active | included above |
| `lexgion_set_top_trace_bit_domain_event` | Skipped by `SHOULD_TRACE` | ~0% |
| LTTng tracepoint emission | N/A in MONITORING | ~0% |
