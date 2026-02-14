# PInsight Trace Configuration Format

PInsight uses an enhanced INI-style configuration file to control tracing behavior at runtime.

## File Structure

The configuration file consists of sections. Each section targets a specific tracing scope (Domain or Code Region/Lexgion).

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
- **`Domain.Kind(Range)`**: Specific configuration for a subset of parallel units (e.g., `OpenMP.thread(0-3)`).
- **`Lexgion.default`**: Default configuration for all code regions.
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

#### Lexgion Configuration
- **Tracing Rate**: `tracing_rate = N` (Trace 1 out of N executions)
- **Max Traces**: `max_num_traces = N`
- **Start Delay**: `trace_starts_at = N`
- **Event Override**: `Domain.Event = on|off`

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

### 3. Removing a Lexgion Trace
Stop tracing a specific code region.
```ini
[REMOVE Lexgion(0x4010bd)]
```
