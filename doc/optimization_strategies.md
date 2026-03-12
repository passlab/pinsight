# PInsight Overhead Optimization Strategies

**Context**: LULESH benchmarks show MONITORING overhead of 4-16% (1T-6T), nearly identical to TRACING overhead. The bottleneck is callback dispatch + lexgion bookkeeping, not LTTng tracepoint emission.

---

## Strategy 1: Selective Callback Deregistration in MONITORING Mode ✅ Implemented

Deregister non-essential callbacks when mode is MONITORING. Only keep:
- `thread_begin/end` — lifecycle management
- `parallel_begin/end` — region counting, lexgion stack
- `implicit_task` — thread tracking within regions

Deregister in MONITORING: `sync_region`, `sync_region_wait`, `work`, `masked`, `mutex_released`, `task_create`, `task_schedule`, `dependences`, `target*`, `device*`, `control_tool`.

**Result**: Reduces 6T overhead from 15.5% → 11.1% (~28% reduction). Modest at lower thread counts because `parallel_begin/end` + `implicit_task` dominate — they fire 44×N times per iteration for LULESH's 44 parallel regions.

---

## Strategy 2: Replace Array Stack with Parent-Pointer Linked Stack

### Motivation

The current per-thread lexgion stack is a fixed-size array (`lexgion_stack[16]`) with an integer `stack_top`. It's used to pass lexgion records between paired `_begin`/`_end` callbacks. However:

1. **OMPT data pointers already carry records**: `parallel_data->ptr`, `task_data->ptr`, and `parent_task->ptr` carry `lexgion_record_t *` through the OMPT runtime — these are redundant with the stack.
2. **`top_lexgion_type()` walks are redundant**: `parallel_end` uses `top_lexgion_type(implicit_task)` to find the enclosing task, but `parent_task->ptr` already provides it (proven by the existing `assert` on line 490).
3. **`enclosing_work_lgp` is already a thread-local**: sync_region's trace inheritance uses a direct thread-local variable, not the stack.
4. **`work`/`masked` end callbacks use the stack only to check "was a lexgion pushed?"** — this can be replaced with a thread-local flag.

### Proposed Change

Add a `parent` pointer to `lexgion_record_t`:

```c
typedef struct lexgion_record_t {
  lexgion_t *lgp;
  unsigned int record_id;
  struct lexgion_record_t *parent;  // ← link to enclosing record
} lexgion_record_t;
```

Records form a linked list through the OMPT data pointers:
- **Push**: `record->parent = current_top; current_top = record; data->ptr = record;`
- **Pop**: `current_top = record->parent;`
- **Walk**: follow `parent` chain

### Comparison: Array Stack vs. Parent-Pointer Stack

| Aspect | Current (array) | Proposed (parent pointer) |
|--------|----------------|--------------------------|
| Max depth | Fixed 16 | Unlimited |
| Push/pop | Array index ±1 | Pointer assignment |
| `top_lexgion_type` | Walk array backwards | Follow parent chain |
| Cache locality | Better (contiguous) | Same in practice (TLS, L1) |
| OMPT integration | Separate from OMPT data ptrs | OMPT `data->ptr` IS the stack |
| Memory | 16 × record always allocated | One pointer per active record |
| Extensibility | Must increase MAX_DEPTH | Automatic |

### Performance Impact

**Negligible**. Push/pop differ by ~1 store (2 stores vs 1 increment + 1 store). `top_lexgion_type` walks 2-3 entries via pointer chase vs array indexing — both in L1 cache. These operations are dwarfed by callback dispatch and `find_lexgion` hash lookup.

### Benefits for Future Work

- Natural support for task-parallel nesting (arbitrary depth)
- Cleaner OMPT integration (records live in OMPT data pointers)
- No fixed stack size limit when extending to new callback types (target, task_create, etc.)
- Potential to eliminate `implicit_task` in MONITORING mode (Strategy 2b) since `parallel_data->ptr->parent` chains naturally

---

## Strategy 3: Cache Config Resolution
Replace linear search in `lexgion_address_trace_config` with a hash map for O(1) lookup by `codeptr_ra`.

**Expected impact**: Small-moderate — depends on number of distinct lexgions.

## Strategy 4: Per-Thread Counter Batching
Use thread-local counters, merge to shared `lgp->counter` periodically instead of every callback.

**Expected impact**: Small — reduces contention on shared counter.

---

## Overhead Breakdown (LULESH, 6T)

| Component | Overhead contribution |
|-----------|----------------------|
| `implicit_task` callbacks (6×44 per iteration) | **High** — largest remaining cost after #1 |
| `parallel_begin/end` callbacks (44 per iteration) | Medium — master thread only |
| `lexgion_begin`/`lexgion_end` (hash + stack) | Medium — per-callback |
| `lexgion_set_top_trace_bit_domain_event` | Low — already skipped by `SHOULD_TRACE` |
| Sync/work/barrier callbacks (eliminated by #1) | ~4% at 6T |
| LTTng tracepoint emission | Very low |
