# PInsight Trace Configuration Format

PInsight uses an enhanced INI-style configuration file to control tracing behavior at runtime.

## File Structure

The configuration file consists of sections. Each section targets a specific tracing scope, which could be a Domain or a Code Region/Lexgion. A domain, such as OpenMP, MPI, or CUDA, defines a set of events (omp_parallel_begin, omp_parallel_end, etc.) and a group of parallel units (e.g. team, thread, device for OpenMP). A code region/lexgion enclose a specific piece of code, e.g. a function, a parallel region. 

### Section Header Format

```ini
[ACTION Target] : Inheritance : PunitSet
```

#### 1. ACTION (Optional)
Specifies how the new configuration interacts with the existing configuration.
- **ADD** (Default): If a configuration exists, merges the new settings with it. If not, adds a new configuration.
- **REPLACE**: If a configuration exists, resets it to default state before applying new settings. If not, adds a new configuration.
- **REMOVE**: Disables and removes configuration for the target.

#### 2. Target
Specifies what is being configured.
- **`Domain.default`**: Global configuration for a domain (e.g., `OpenMP.default`).
- **`Domain.Kind(Range)`**: Specific configuration for a subset of parallel units (e.g., `OpenMP.team(0-4), OpenMP.thread(0-3)`, `MPI.rank(0-4)`).
- **`Lexgion.default`**: Default configuration for all code regions across all domains.
- **`Lexgion(Domain).default`**: Default lexgion configuration for a specific domain (e.g., `Lexgion(OpenMP).default`). This provides domain-specific defaults for event on/off and rate tracing that apply to all lexgions of that domain. No inheritance or PunitSet is needed for this section type.
- **`Lexgion(Address)`**: Configuration for a specific code region by address (e.g., `Lexgion(0x400500)`).

#### 3. Inheritance (Optional)
A list of defaults to inherit from, separated by commas.
- Example: `OpenMP.default, MPI.default`

#### 4. PunitSet (Optional)
Additional constraints from other domains, separated by commas.
- Format: `Domain.Kind(Range)`
- Example: `MPI.rank(0), CUDA.device(0)`

### Section Body (Key-Value Pairs)

#### Domain Configuration
- **Event Control**: `EventName = on|off`
- **Punit Range Override** (Only in `Domain.default`): `Domain.Kind = (Range)`

#### Lexgion Configuration (applies to `Lexgion.default`, `Lexgion(Domain).default`, and `Lexgion(Address)`)
- **Tracing Rate**: `tracing_rate = N` (Trace 1 out of N executions)
- **Max Traces**: `max_num_traces = N`
- **Start Delay**: `trace_starts_at = N`
- **Event Control**: `EventName = on|off` (enable/disable specific events for lexgions of this domain)

---

## Examples

### 1. Replacing a Domain Configuration
Completely reset OpenMP configuration and apply new settings.
```ini
[REPLACE OpenMP.default]
    OpenMP.team = (0-4)
    omp_task_create = on
```

### 2. Adding Specific Thread Tracing
Add tracing config for threads 0-3 without affecting other threads.
```ini
[ADD OpenMP.thread(0-3)] : OpenMP.default
    omp_task_schedule = on
```

### 3. Setting Domain-Specific Lexgion Defaults
Set default tracing behavior for all OpenMP lexgions, including which events to trace and the rate tracing parameters.
```ini
[Lexgion(OpenMP).default]
    omp_thread_begin = on
    omp_thread_end = on
    omp_task_create = on
    omp_task_schedule = on
    trace_starts_at = 0
    max_num_traces = 2000
    tracing_rate = 1
```

### 4. Removing a Lexgion Trace
Stop tracing a specific code region.
```ini
[REMOVE Lexgion(0x4010bd)]
```
