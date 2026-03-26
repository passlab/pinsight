# Design: `pinsight_begin` / `pinsight_end` User Instrumentation API

## Goal

Allow users to manually instrument code regions for PInsight tracing, complementing
the automatic interposition (OMPT, PMPI, CUPTI) with explicit begin/end markers.

---

## 1. API Signature

### Recommended API

```c
#include <pinsight_user.h>

void pinsight_region_begin(const char *name);
void pinsight_region_end(const char *name);
```

### Arguments

| Argument | Type | Purpose |
|----------|------|---------|
| `name` | `const char *` | Region identifier (e.g., `"solver_phase"`, `"matrix_multiply"`) |

### Rationale

- **Name-based identification** instead of codeptr — user regions don't have runtime return
  addresses like OMPT callbacks. The name string serves as the stable, human-readable identifier.
- **Name in both begin/end** — enables validation (mismatched begin/end detection) and supports
  interleaved regions across different code paths.
- **No `type` parameter needed** — for user regions, the class is always `USER_LEXGION` and
  there's a single type (e.g., `USER_REGION_TYPE = 0`).

### Alternative: Name + Category

```c
void pinsight_region_begin(const char *category, const char *name);
void pinsight_region_end(const char *category, const char *name);
```

This would allow users to group regions (e.g., `"solver"`, `"io"`, `"comm"`) and enable
category-level config. **Defer this to a future extension** — start simple with just `name`.

### Alternative: Return codeptr automatically

```c
// Use __builtin_return_address(0) internally to get the call site
void pinsight_region_begin(const char *name);
// Internally: codeptr_ra = __builtin_return_address(0);
```

This gives each call site a unique binary address, allowing address-specific config like
OpenMP regions. The name is then used only for display/config file matching.

> [!IMPORTANT]
> **Design decision**: Should lexgions be identified by **name** or by **codeptr** (call site address)?
>
> | Approach | Pros | Cons |
> |----------|------|------|
> | **Name only** | Stable across rebuilds, human-readable config, same name = same region | Hash table lookup, string comparison overhead |
> | **Codeptr only** | Consistent with OMPT, existing lookup code works | Changes on recompile, not human-readable |
> | **Both** (recommended) | Name for config matching + display, codeptr for fast lookup | Slightly more complex, best of both worlds |
>
> **Recommendation**: Use `__builtin_return_address(0)` as the primary key (like OMPT), but
> store the `name` string in the lexgion for config file matching and trace output. This approach
> reuses the existing `find_lexgion()` fast-path lookup by address, while allowing name-based
> config in the config file.

---

## 2. Lexgion Identification & Lookup

### Option A: Codeptr-primary (recommended)

```c
void pinsight_region_begin(const char *name) {
    const void *codeptr = __builtin_return_address(0);
    lexgion_record_t *rec = lexgion_begin(USER_LEXGION, USER_REGION_TYPE, codeptr);
    // Store name in lexgion on first encounter:
    if (rec->lgp->name[0] == '\0')
        strncpy(rec->lgp->name, name, MAX_REGION_NAME_LEN);
    // ... rate-limit check, LTTng tracepoint ...
}
```

**Requires**: Add a `char name[64]` field to `lexgion_t` (only populated for `USER_LEXGION`).

### Option B: Name-primary (hash table)

```c
void pinsight_region_begin(const char *name) {
    lexgion_t *lgp = find_or_create_user_lexgion_by_name(name);
    // ... push, rate-limit, trace ...
}
```

**Requires**: A hash table mapping `name → lexgion_t*`. Thread-safe with reader-writer lock.

### Recommendation

**Option A (codeptr-primary)** — reuses the existing `lexgion_begin()` infrastructure with minimal
changes. The name is supplementary for config matching and trace output.

---

## 3. Domain Registration: "User" Domain

### New Domain Index

Add `User` as a new domain in the `domain_info_table`:

```c
// trace_config.h — extend domain enum comments
// Domain indices: 0=OpenMP, 1=MPI, 2=CUDA, ..., N=User
#define PINSIGHT_DOMAIN_USER_IDX  <next available index>   // e.g. 3 or 7
```

### domain_info_table entry

```c
// trace_config.c or a new user_domain.c
domain_info_table[PINSIGHT_DOMAIN_USER_IDX] = (struct domain_info){
    .name = "User",
    .num_events = 2,       // begin, end
    .event_table = { ... },
    .starting_mode = PINSIGHT_DOMAIN_TRACING,
    .eventInstallStatus = ...,
};
```

### Environment Variable

```bash
PINSIGHT_TRACE_USER=TRACING    # (default)
PINSIGHT_TRACE_USER=MONITORING # bookkeeping only
PINSIGHT_TRACE_USER=OFF        # skip entirely
```

This gives users domain-level control (OFF/MONITORING/TRACING) for all user-instrumented regions,
just like `PINSIGHT_TRACE_OPENMP`.

---

## 4. LTTng Tracepoint Design

### New Tracepoint Provider

Create `user_lttng_ust_tracepoint.h`:

```c
LTTNG_UST_TRACEPOINT_EVENT(
    pinsight_user_lttng_ust,
    region_begin,
    LTTNG_UST_TP_ARGS(
        const char *, region_name,
        const void *, codeptr,
        unsigned int, record_id,
        unsigned int, counter
    ),
    LTTNG_UST_TP_FIELDS(
        COMMON_LTTNG_UST_TP_FIELDS_GLOBAL
        COMMON_LTTNG_UST_TP_FIELDS_OMP
        lttng_ust_field_string(region_name, region_name)
        lttng_ust_field_integer_hex(unsigned long, codeptr, (unsigned long)codeptr)
        lttng_ust_field_integer(unsigned int, record_id, record_id)
        lttng_ust_field_integer(unsigned int, counter, counter)
    )
)

LTTNG_UST_TRACEPOINT_EVENT(
    pinsight_user_lttng_ust,
    region_end,
    LTTNG_UST_TP_ARGS(
        const char *, region_name,
        const void *, codeptr,
        unsigned int, record_id,
        unsigned int, counter
    ),
    LTTNG_UST_TP_FIELDS(
        COMMON_LTTNG_UST_TP_FIELDS_GLOBAL
        COMMON_LTTNG_UST_TP_FIELDS_OMP
        lttng_ust_field_string(region_name, region_name)
        lttng_ust_field_integer_hex(unsigned long, codeptr, (unsigned long)codeptr)
        lttng_ust_field_integer(unsigned int, record_id, record_id)
        lttng_ust_field_integer(unsigned int, counter, counter)
    )
)
```

**Key fields**:
- `region_name` — the user-specified string identifier
- `codeptr` — call site address for symbol resolution
- `record_id` — links begin/end pairs (from lexgion counter)
- `counter` — total invocation count

**LTTng session**: `lttng enable-event -u 'pinsight_user_lttng_ust:*'`

---

## 5. trace_config Integration

### Config File: Name-Based Sections

Since user regions are identified by name, the config file should support **name-based** lexgion
sections in addition to address-based sections:

```ini
# Domain-level: control all user regions
[User.global]
    trace_mode = TRACING

# Default for all user regions
[Lexgion(User).default]
    max_num_traces = 200
    tracing_rate = 10

# Name-specific config (NEW: match by region name)
[Lexgion(User).solver_phase]
    max_num_traces = 500
    tracing_rate = 1
    trace_mode_after = PAUSE:30:analyze_solver.sh:MONITORING

[Lexgion(User).matrix_multiply]
    max_num_traces = 50
    tracing_rate = 5
    trace_mode_after = MONITORING
```

### Config Resolution Order

```
[Lexgion(User).solver_phase]     ← name-specific (highest priority)
    ⊕ [Lexgion(User).default]    ← domain default
    ⊕ [Lexgion.default]          ← global default
```

### Implementation in trace_config_parse.c

The config parser already handles `[Lexgion(Domain).ADDR]` sections where `ADDR` is a hex address.
For user regions, extend this to accept **name strings** as identifiers:

```c
// In parse_section_header():
// Existing: [Lexgion(OpenMP).0x401234]  → match by codeptr
// New:      [Lexgion(User).solver_phase] → match by name string
```

Store name-based configs in a separate lookup (e.g., a small array or hash map). At
`pinsight_region_begin()`, after resolving the lexgion, check if there's a name-based
config that matches `lgp->name`, and if so, override the default config.

### Interaction with Existing Features

| Feature | Behavior with User Regions |
|---------|---------------------------|
| **Rate-limiting** (`max_num_traces`, `tracing_rate`) | Works — same `trace_bit` mechanism |
| **Mode switching** (`trace_mode_after`) | Works — triggers when user region hits `max_num_traces` |
| **PAUSE action** | Works — pauses, rotates, runs script when user region limit reached |
| **Event filtering** | Not applicable (User domain has only begin/end events) |
| **SIGUSR1 reload** | Works — `[User.global]` trace_mode changes take effect on reload |
| **Domain OFF** | `PINSIGHT_TRACE_USER=OFF` → `pinsight_region_begin()` returns early |

---

## 6. Implementation Sketch

### pinsight_region_begin()

```c
void pinsight_region_begin(const char *name) {
    // 1. Domain kill-switch
    if (domain_default_trace_config[PINSIGHT_DOMAIN_USER_IDX].mode
        == PINSIGHT_DOMAIN_OFF)
        return;

    // 2. Push lexgion (reuses existing infrastructure)
    const void *codeptr = __builtin_return_address(0);
    lexgion_record_t *rec = lexgion_begin(USER_LEXGION, 0, codeptr);
    lexgion_t *lgp = rec->lgp;

    // 3. Store name on first encounter
    if (lgp->name[0] == '\0')
        strncpy(lgp->name, name, sizeof(lgp->name) - 1);

    // 4. Resolve trace config (uses name-based lookup for User domain)
    if (lgp->trace_config == NULL)
        lgp->trace_config = lexgion_set_trace_config(lgp, PINSIGHT_DOMAIN_USER_IDX);

    // 5. Rate-limit check
    lgp->counter++;
    int trace_bit = lexgion_set_rate_trace_bit(lgp);
    lgp->trace_bit = trace_bit;

    // 6. Emit LTTng tracepoint
    if (trace_bit && PINSIGHT_SHOULD_TRACE(PINSIGHT_DOMAIN_USER_IDX)) {
        lttng_ust_tracepoint(pinsight_user_lttng_ust, region_begin,
                             name, codeptr, rec->record_id, lgp->counter);
        lexgion_post_trace_update(lgp);
    }
}
```

### pinsight_region_end()

```c
void pinsight_region_end(const char *name) {
    if (domain_default_trace_config[PINSIGHT_DOMAIN_USER_IDX].mode
        == PINSIGHT_DOMAIN_OFF)
        return;

    unsigned int record_id;
    lexgion_t *lgp = pop_lexgion(&record_id);
    if (!lgp) return;

    if (lgp->trace_bit && PINSIGHT_SHOULD_TRACE(PINSIGHT_DOMAIN_USER_IDX)) {
        lttng_ust_tracepoint(pinsight_user_lttng_ust, region_end,
                             name, lgp->codeptr_ra, record_id, lgp->counter);
    }
}
```

---

## 7. Thread Safety

- `pinsight_thread_data` is already `__thread` — per-thread lexgion stack is lock-free
- `find_lexgion()` lookup is per-thread (no shared state)
- Name-based config lookup is read-only after init (config file parsed once at startup)
- The existing `config_reload_requested` + deferred reload mechanism handles SIGUSR1

---

## 8. New Files

| File | Purpose |
|------|---------|
| `src/pinsight_user.h` | Public API header: `pinsight_region_begin()` / `pinsight_region_end()` |
| `src/pinsight_user.c` | Implementation + `LTTNG_UST_TRACEPOINT_CREATE_PROBES` |
| `src/user_lttng_ust_tracepoint.h` | LTTng tracepoint definitions for User domain |

### Changes to existing files

| File | Change |
|------|--------|
| `pinsight.h` | Add `char name[64]` to `lexgion_t` (for USER_LEXGION only) |
| `trace_config.h` | Add `PINSIGHT_DOMAIN_USER_IDX` |
| `trace_config.c` | Register "User" in `domain_info_table[]`, parse `PINSIGHT_TRACE_USER` env |
| `trace_config_parse.c` | Support name-based `[Lexgion(User).region_name]` sections |
| `CMakeLists.txt` | Add `pinsight_user.c` to build, add `PINSIGHT_USER` compile flag |

---

## 9. Language Compatibility

```c
// C
pinsight_region_begin("solver");
solve();
pinsight_region_end("solver");
```

```cpp
// C++ — same API via extern "C"
pinsight_region_begin("matrix_op");
matrix.multiply(a, b);
pinsight_region_end("matrix_op");
```

```fortran
! Fortran — needs C binding wrapper
call pinsight_region_begin("timestep"//C_NULL_CHAR)
call timestep(...)
call pinsight_region_end("timestep"//C_NULL_CHAR)
```

---

## 10. Open Questions for User

1. **Name requirement in `_end`**: Should `pinsight_region_end()` require the name, or should
   it just pop the lexgion stack (simpler but no mismatch detection)?

2. **Nesting**: Should nested user regions be supported? The lexgion stack
   already supports depth 16, but naming collisions need consideration:
   ```c
   pinsight_region_begin("outer");
   pinsight_region_begin("inner");  // fine — different name, different codeptr
   pinsight_region_end("inner");
   pinsight_region_end("outer");
   ```

3. **Category/group parameter**: Defer or include from the start?

4. **Build flag**: Should user API require `PINSIGHT_USER=ON` at build time,
   or should it always be available when linking `libpinsight.so`?
