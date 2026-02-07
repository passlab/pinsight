# PINSIGHT Trace Config Format

This document specifies the trace configuration file format used by PInsight. It is based on the example and inline description in `src/trace_config_example.txt`.

## Format Overview

The trace config file is an enhanced INI-style file:

- `#` starts a comment line.
- Sections are declared with `[section_name]`.
- Section content is `key = value` pairs.
- Whitespace around keys/values is ignored.

A section name has up to three colon-separated parts:

```
[<section_spec> : <inheritance_list> : <punit_constraints>]
```

Only the first part is required; the inheritance list and punit constraints are optional.

## Section Name Parts

### 1) Section Specification (`section_spec`)

The section spec identifies what is being configured. It can be one of:

- Domain default: `<domain_name>.default`
  - Example: `OpenMP.default`, `MPI.default`, `CUDA.default`

- Domain punit: `<domain_name>.<punit_kind>(<punit_range>)`
  - Example: `OpenMP.thread(0-3)`, `MPI.rank(0)`, `CUDA.device(0)`

- Lexgion: `Lexgion(<code_ptr>)`
  - Example: `Lexgion(0x4010bd)`

### 2) Inheritance List (`inheritance_list`)

The inheritance list is a comma-separated list of domain default sections the current section inherits from:

- Format: `<domain_name>.default, <domain_name>.default, ...`
- Example: `OpenMP.default, MPI.default`

For lexgion sections, the inheritance list may include multiple domain defaults.

### 3) Punit Constraints (`punit_constraints`)

Constraints limit when the section applies, based on punits from other domains. It is a comma-separated list:

- Format: `<domain_name>.<punit_kind>(<punit_range>), ...`
- Example: `MPI.rank(0), CUDA.device(0)`

## Punit Range Syntax

A punit range can be:

- Single number: `5`
- Inclusive range: `0-4`
- List of numbers and ranges: `4, 6, 8-12, 14-16, 20-24`

Whitespace is allowed around commas and dashes.

## Section Content

### Domain Default Sections

Use a `<domain_name>.default` section to define the default configuration for a domain:

- Punit ranges for that domain:
  - `OpenMP.team = (0-4)`
  - `OpenMP.thread = (0, 15)`
  - `MPI.rank = (0-3)`
- Event toggles (`on`/`off`) for that domain:
  - `omp_task_create = off`
  - `CUDA_memcpy = on`

Example:

```ini
[OpenMP.default]
    OpenMP.team = (0-4)
    OpenMP.thread = (0, 15)
    OpenMP.device = (0-3)
    omp_task_create = off
    omp_task_schedule = off
```

### Domain Punit Sections

A punit section configures a subset of punits for a domain, optionally inheriting defaults and constraining by other domains’ punits.

Example:

```ini
[OpenMP.thread(0-3): OpenMP.default: MPI.rank(0), CUDA.device(0)]
```

### Lexgion Sections

A lexgion section configures a specific code region identified by a code pointer. It can inherit from domain defaults and add constraints.

Lexgion configuration keys include:

- `trace_starts_at`: invocation count to begin tracing
- `max_num_traces`: maximum number of traces to collect
- `tracing_rate`: trace 1 out of every N invocations

A lexgion section can also enable/disable specific domain events by prefixing the event name with the domain name.

Example:

```ini
[Lexgion(0x4010bd): Lexgion.default, OpenMP.default, MPI.default: MPI.rank(0), CUDA.device(0)]
    trace_starts_at = 20
    max_num_traces = 200
    tracing_rate = 10
    OpenMP.omp_thread_begin = on
    OpenMP.omp_thread_end = on
    MPI.MPI_rank_begin = on
    MPI.MPI_rank_end = on
    CUDA.CUDA_kernel_launch = on
    CUDA.CUDA_memcpy = on
```

### Lexgion Default Sections

You can define default lexgion behavior using a lexgion default section (as shown in the example):

```ini
[Lexgion.default: OpenMP.default, MPI.default]
    trace_starts_at = 0
    max_num_traces = 2000
    tracing_rate = 1
```

## Summary of Parsing Behavior (Implementation Notes)

- Parse comments, section headers, and key-value pairs.
- Use `<domain_name>.default` sections to populate the domain trace configuration table.
- Parse punit-specific sections into per-punit configurations linked by a `next` pointer.
- Update domain punit range metadata using `<domain_name>.<punit_kind> = (<range>)` entries found in domain defaults.
- Parse lexgion sections into the lexgion configuration table keyed by code pointer.
- Validate domain names and punit kinds against the domain info table before applying.

## Example (Complete)

```ini
[OpenMP.default]
    OpenMP.team = (0-4)
    OpenMP.thread = (0, 15)
    OpenMP.device = (0-3)
    omp_task_create = off
    omp_task_schedule = off

[MPI.default]
    MPI.rank = (0-3)

[CUDA.default]
    CUDA.device = (0-3)
    CUDA_kernel_launch  = on
    CUDA_memcpy = on

[Lexgion.default: OpenMP.default, MPI.default]
    trace_starts_at = 0
    max_num_traces = 2000
    tracing_rate = 1

[OpenMP.thread(0-3): OpenMP.default: MPI.rank(0), CUDA.device(0)]

[OpenMP.thread(4, 6, 8-12, 14-16, 20-22): OpenMP.default: MPI.rank(0), CUDA.device(0)]

[OpenMP.team(0-4), OpenMP.thread(0-15): OpenMP.default: MPI.rank(0), CUDA.device(0)]

[OpenMP.device(0): OpenMP.default]

[MPI.rank(0): MPI.default]

[CUDA.device(0): CUDA.default]

[Lexgion(0x4010bd): Lexgion.default, OpenMP.default, MPI.default: MPI.rank(0), CUDA.device(0)]
    trace_starts_at = 20
    max_num_traces = 200
    tracing_rate = 10
    OpenMP.omp_thread_begin = on
    OpenMP.omp_thread_end = on
    MPI.MPI_rank_begin = on
    MPI.MPI_rank_end = on
    CUDA.CUDA_kernel_launch = on
    CUDA.CUDA_memcpy = on
```
