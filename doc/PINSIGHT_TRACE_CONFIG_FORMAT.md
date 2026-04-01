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
- **`Domain.global`**: Domain-wide structural settings — trace mode and punit scope (e.g., `OpenMP.global`).
- **`Domain.default`**: Default event configuration for a domain (e.g., `OpenMP.default`).
- **`Domain.PunitKind(PunitSet)`**: Configuration for a subset of parallel units (e.g., `OpenMP.team(0-3, 7, 12-20)`, `MPI.rank(0-4)`).
- **`Lexgion.default`**: Default configuration for all code regions across all domains.
- **`Lexgion(Domain).default`**: Default lexgion configuration for a specific domain (e.g., `Lexgion(OpenMP).default`). Eagerly initialized as `Lexgion.default ⊕ Domain.default` (rate triple from global default, events from domain default).
- **`Lexgion(Address)` or `Lexgion(Addr1, Addr2, ...)`**: Configuration for one or more specific code regions by address (e.g., `Lexgion(0x400500)` or `Lexgion(0x400500, 0x400600)`). When multiple addresses are listed, each gets its own config but shares the same section body settings.

> [!IMPORTANT]
> Sections should appear in the config file in the order listed above: `Domain.global` → `Domain.default` → `Domain.PunitKind(Set)` → `Lexgion.default` → `Lexgion(Domain).default` → `Lexgion(Address)`. This is required because all inheritance is resolved at parse time as a snapshot copy of the referenced target's current state. A section that inherits from or depends on a target defined later in the file will see stale or uninitialized defaults.


##### Action-Target Validity

| Target Type | SET | RESET | REMOVE |
|---|---|---|---|
| `Domain.global` | ✅ Merge settings | ✅ Revert mode to install default | ❌ Invalid (use RESET) |
| `Domain.default` | ✅ Merge settings | ✅ Revert to system install defaults | ❌ Invalid (use RESET) |
| `Lexgion.default` | ✅ Merge settings | ✅ Revert to system defaults | ❌ Invalid (use RESET) |
| `Lexgion(Domain).default` | ✅ Merge settings | ✅ Revert to `Lexgion.default ⊕ Domain.default` | ❌ Invalid (use RESET) |
| `Domain.PunitKind(Set)` | ✅ Merge settings | ❌ Invalid (use REMOVE) | ✅ Clear/disable this punit-specific config so they will use default settings in the future |
| `Domain.PunitKind(*)` | ❌ Invalid | ❌ Invalid | ✅ Remove ALL configs of this punit kind so they will use default settings in the future |
| `Lexgion(Address)` | ✅ Merge settings | ❌ Invalid (use REMOVE) | ✅ Mark lexgion-specific config as removed so they will use default settings in the future |

##### RESET Semantics by Target

| Target | RESET reverts to... |
|---|---|
| `Domain.global` | Install defaults: mode = TRACING if domain has registered events, OFF otherwise |
| `Domain.default` | System install defaults (events as registered by the domain) |
| `Lexgion.default` | System install defaults: `tracing_rate=1`, `trace_starts_at=0`, `max_num_traces=-1`, all event overrides cleared |
| `Lexgion(Domain).default` | Computed default: rate triple from `Lexgion.default` + events from `Domain.default` |

#### 3. Inheritance (Optional)
Specifies domain defaults to inherit events from, separated by commas. **Applies to all `Lexgion(*)` section types** (including `Lexgion.default` and `Lexgion(Domain).default`) and `Domain.PunitKind(Set)` sections. Does **not** apply to `Domain.default`.
- Example: `OpenMP.default, MPI.default`

> **Note:** Inheritance is applied at parse time as a **snapshot copy**. If a default section is modified later in the configuration file, sections that already inherited from it (earlier in the file) are **not** retroactively updated. Only sections defined after the change will see the new defaults.

#### 4. PunitSet (Optional)
Additional punit constraints from other domains, separated by commas. **Only applies to `Domain.PunitKind(Set)` and `Lexgion(Address)` sections** — Lexgion default sections do not use PunitSet.
- Format: `Domain.PunitKind(PunitSet)`
- Example: `MPI.rank(0-3, 8-15, 63), CUDA.device(0)`

### Section Body (Key-Value Pairs)

#### Domain Global Configuration (`Domain.global`)
- **Trace Mode**: `trace_mode = OFF|MONITORING|TRACING`
- **Punit Scope**: `Domain.PunitKind = (Range)` (e.g., `OpenMP.thread = (0-15)`)

#### Domain Default Configuration (`Domain.default`)
- **Event Control**: `EventName = on|off`

#### Lexgion Configuration (applies to `Lexgion.default`, `Lexgion(Domain).default`, and `Lexgion(Address)`)
- **Tracing Rate**: `tracing_rate = N` (Trace 1 out of N executions)
- **Max Traces**: `max_num_traces = N`
- **Start Delay**: `trace_starts_at = N`
- **Auto Mode Switch**: `trace_mode_after = MODE` — Automatically switch domain trace modes when `max_num_traces` is reached. Supports:
  - Shorthand: `trace_mode_after = MONITORING` (applies to all domains with events in the lexgion)
  - Explicit per-domain: `trace_mode_after = OpenMP:MONITORING, MPI:OFF`
  - **INTROSPECT action**: `trace_mode_after = INTROSPECT:timeout:script[:resume_mode]`
    - `timeout` — seconds to wait before auto-resuming (0 = wait indefinitely for SIGUSR1)
    - `script` — analysis script to launch (`-` = none). Receives `<chunk_path> <app_pid> <config_file>` as arguments.
    - `resume_mode` — domain mode after resume (default: `MONITORING`). Accepts `OFF`, `MONITORING`, or `TRACING`.
    - When INTROSPECT fires, PInsight automatically runs `lttng rotate` to flush traces, then optionally launches the script, then blocks until timeout or SIGUSR1.
- **Event Control**: `EventName = on|off`

---

## Examples

### 1. Setting Domain-Wide Configuration
Set trace mode and punit scope for the OpenMP domain.
```ini
[OpenMP.global]
    trace_mode = TRACING
    OpenMP.team = (0-4)
    OpenMP.thread = (0, 15)
```

### 2. Disabling a Domain at Runtime
Send `kill -USR1 <pid>` after editing the config file to disable OpenMP tracing with zero overhead.
```ini
[OpenMP.global]
    trace_mode = OFF
```

### 3. Resetting Domain Mode to Install Default
Revert mode to TRACING (if events are registered).
```ini
[RESET OpenMP.global]
```

### 4. Setting Domain Event Configuration
Merge new event settings with existing OpenMP configuration.
```ini
[OpenMP.default]
    omp_task_create = on
```

### 5. Resetting Domain Events to Install Defaults
Revert OpenMP event configuration back to system install defaults.
```ini
[RESET OpenMP.default]
```

### 6. Adding Specific Thread Tracing
Set tracing config for threads 0-3 without affecting other threads.
```ini
[OpenMP.thread(0-3)] : OpenMP.default
    omp_task_schedule = on
```

### 7. Setting Domain-Specific Lexgion Defaults
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

### 8. Resetting a Domain-Specific Lexgion Default
Revert to computed default (`Lexgion.default ⊕ OpenMP.default`).
```ini
[RESET Lexgion(OpenMP).default]
```

### 9. Removing a Lexgion Trace
Stop tracing a specific code region.
```ini
[REMOVE Lexgion(0x4010bd)]
```

### 10. Configuring Multiple Lexgions at Once
Apply the same settings to multiple code regions.
```ini
[Lexgion(0x400500, 0x400600, 0x400700)]
    max_num_traces = 100
    tracing_rate = 5
```

### 11. Removing Multiple Lexgions at Once
```ini
[REMOVE Lexgion(0x400500, 0x400600)]
```

### 12. Removing a Punit-Specific Config
Remove the thread-specific config; those threads fall back to domain default.
```ini
[REMOVE OpenMP.thread(0-3)]
```

### 13. Removing All Thread Configs (Wildcard)
Remove all `OpenMP.thread(*)` configs without knowing individual sets.
```ini
[REMOVE OpenMP.thread(*)]
```

### 14. Automatic Mode Switching After Tracing
Trace 100 executions of each lexgion, then switch all domains to MONITORING.
```ini
[Lexgion.default]
    max_num_traces = 100
    tracing_rate = 1
    trace_mode_after = MONITORING
```

### 15. Per-Domain Auto Mode Switch
Trace 50 executions of a specific region, then set OpenMP to MONITORING and MPI to OFF.
```ini
[Lexgion(0x400500)]
    max_num_traces = 50
    trace_mode_after = OpenMP:MONITORING, MPI:OFF
```

### 16. Introspect-Analyze-Resume Workflow
Trace 100 executions, then introspect for 60 seconds while running an analysis script. The app resumes in TRACING mode after the script sends SIGUSR1 or the timeout expires. PInsight automatically runs `lttng rotate` before launching the script.
```ini
[Lexgion.default]
    max_num_traces = 100
    tracing_rate = 10
    trace_mode_after = INTROSPECT:60:analyze_traces.sh:TRACING
```

### 17. INTROSPECT via Environment Variable
Same as above, configured entirely via env var:
```bash
PINSIGHT_TRACE_RATE=0:100:10:INTROSPECT:60:analyze_traces.sh:TRACING
```

### 18. Indefinite INTROSPECT (Interactive Debugging)
Introspect indefinitely with no script — only SIGUSR1 resumes the app.
```ini
[Lexgion.default]
    max_num_traces = 50
    trace_mode_after = INTROSPECT:0:-
```
