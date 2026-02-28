# PInsight Trace Configuration Format

PInsight uses an enhanced INI-style configuration file to control tracing behavior at runtime.

## File Structure

The configuration file consists of sections. Each section targets a specific tracing scope, which could be a Domain or a Code Region/Lexgion. A domain, such as OpenMP, MPI, or CUDA, defines a set of events (omp_parallel_begin, omp_parallel_end, etc.) and a group of parallel units (e.g. team, thread, device for OpenMP). A code region/lexgion enclose a specific piece of code, e.g. a function, a parallel region. 

### Section Header Format

```ini
[ACTION Target] : Inheritance : PunitSets
```

#### 1. ACTION (Optional)
Specifies how the new configuration interacts with the existing configuration. There are three actions:

- **SET** (Default): Merge new settings with existing configuration. Only the fields explicitly specified in the section body are modified; all other fields retain their current values. If no configuration exists for the target, a new one is created.
- **RESET**: Revert the target to its computed/system defaults. No section body is needed. Only valid for `*.default` sections (see validity table below).
- **REMOVE**: Delete or disable the configuration for the target. No section body is needed. Only valid for non-default sections. Supports **wildcard removal**: `[REMOVE Domain.PunitKind(*)]` removes all configs of that punit kind.

#### 2. Target
Specifies what is being configured. There are five types of targets:
- **`Domain.default`**: Default configuration for a domain (e.g., `OpenMP.default`).
- **`Domain.PunitKind(PunitSet)`**: Configuration for a subset of parallel units (e.g., `OpenMP.team(0-3, 7, 12-20)`, `MPI.rank(0-4)`).
- **`Lexgion.default`**: Default configuration for all code regions across all domains.
- **`Lexgion(Domain).default`**: Default lexgion configuration for a specific domain (e.g., `Lexgion(OpenMP).default`). Eagerly initialized as `Lexgion.default ⊕ Domain.default` (rate triple from global default, events from domain default).
- **`Lexgion(Address)` or `Lexgion(Addr1, Addr2, ...)`**: Configuration for one or more specific code regions by address (e.g., `Lexgion(0x400500)` or `Lexgion(0x400500, 0x400600)`). When multiple addresses are listed, each gets its own config but shares the same section body settings.
##### Action-Target Validity

| Target Type | SET | RESET | REMOVE |
|---|---|---|---|
| `Lexgion.default` | ✅ Merge settings | ✅ Revert to system defaults | ❌ Invalid (use RESET) |
| `Lexgion(Domain).default` | ✅ Merge settings | ✅ Revert to `Lexgion.default ⊕ Domain.default` | ❌ Invalid (use RESET) |
| `Domain.default` | ✅ Merge settings | ✅ Revert to system install defaults | ❌ Invalid (use RESET) |
| `Domain.PunitKind(Set)` | ✅ Merge settings | ❌ Invalid (use REMOVE) | ✅ Clear/disable this punit config |
| `Domain.PunitKind(*)` | ❌ Invalid | ❌ Invalid | ✅ Remove ALL configs of this punit kind |
| `Lexgion(Address)` | ✅ Merge settings | ❌ Invalid (use REMOVE) | ✅ Mark lexgion as removed (stop tracing) |

##### RESET Semantics by Target

| Target | RESET reverts to... |
|---|---|
| `Lexgion.default` | System defaults: `tracing_rate=1`, `trace_starts_at=0`, `max_num_traces=-1`, all event overrides cleared |
| `Lexgion(Domain).default` | Computed default: rate triple from `Lexgion.default` + events from `Domain.default` |
| `Domain.default` | System install defaults (events as registered by the domain) |

#### 3. Inheritance (Optional)
Specifies domain defaults to inherit events from, separated by commas. **Applies to all `Lexgion(*)` section types** (including `Lexgion.default` and `Lexgion(Domain).default`) and `Domain.PunitKind(Set)` sections. Does **not** apply to `Domain.default`.
- Example: `OpenMP.default, MPI.default`

> **Note:** Inheritance is applied at parse time as a **snapshot copy**. If a default section is modified later in the configuration file, sections that already inherited from it (earlier in the file) are **not** retroactively updated. Only sections defined after the change will see the new defaults.

#### 4. PunitSet (Optional)
Additional punit constraints from other domains, separated by commas. **Only applies to `Domain.PunitKind(Set)` and `Lexgion(Address)` sections** — Lexgion default sections do not use PunitSet.
- Format: `Domain.PunitKind(PunitSet)`
- Example: `MPI.rank(0-3, 8-15, 63), CUDA.device(0)`

### Section Body (Key-Value Pairs)

#### Domain Configuration
- **Event Control**: `EventName = on|off`
- **Punit Range Override** (Only in `Domain.default`): `Domain.PunitKind = (Range)`

#### Lexgion Configuration (applies to `Lexgion.default`, `Lexgion(Domain).default`, and `Lexgion(Address)`)
- **Tracing Rate**: `tracing_rate = N` (Trace 1 out of N executions)
- **Max Traces**: `max_num_traces = N`
- **Start Delay**: `trace_starts_at = N`
- **Event Control**: `EventName = on|off`

---

## Examples

### 1. Setting Domain Configuration (default action is SET)
Merge new settings with existing OpenMP configuration.
```ini
[OpenMP.default]
    OpenMP.team = (0-4)
    omp_task_create = on
```

### 2. Resetting a Domain to Install Defaults
Revert OpenMP configuration back to system install defaults.
```ini
[RESET OpenMP.default]
```

### 3. Adding Specific Thread Tracing
Set tracing config for threads 0-3 without affecting other threads.
```ini
[OpenMP.thread(0-3)] : OpenMP.default
    omp_task_schedule = on
```

### 4. Setting Domain-Specific Lexgion Defaults
Set default tracing behavior for all OpenMP lexgions.
```ini
[Lexgion(OpenMP).default]
    omp_thread_begin = on
    omp_thread_end = on
    omp_task_create = on
    omp_task_schedule = on
    trace_starts_at = 0
    max_num_traces = 200
    tracing_rate = 1
```

### 5. Resetting a Domain-Specific Lexgion Default
Revert to computed default (`Lexgion.default ⊕ OpenMP.default`).
```ini
[RESET Lexgion(OpenMP).default]
```

### 6. Removing a Lexgion Trace
Stop tracing a specific code region.
```ini
[REMOVE Lexgion(0x4010bd)]
```

### 7. Configuring Multiple Lexgions at Once
Apply the same settings to multiple code regions.
```ini
[Lexgion(0x400500, 0x400600, 0x400700)]
    max_num_traces = 100
    tracing_rate = 5
```

### 8. Removing Multiple Lexgions at Once
```ini
[REMOVE Lexgion(0x400500, 0x400600)]
```

### 9. Removing a Punit-Specific Config
Remove the thread-specific config; those threads fall back to domain default.
```ini
[REMOVE OpenMP.thread(0-3)]
```

### 10. Removing All Thread Configs (Wildcard)
Remove all `OpenMP.thread(*)` configs without knowing individual sets.
```ini
[REMOVE OpenMP.thread(*)]
```
