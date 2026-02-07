# PINSIGHT Trace Config Format

This document describes the trace configuration file format used by PInsight. It is based on `src/trace_config_example.txt` and its inline specification comments.

## Overview

The trace config file uses an enhanced INI-style format:

- `#` starts a comment line.
- Sections are declared with `[section_name]`.
- Section content uses `key = value` pairs.
- Whitespace around keys and values is ignored.

A section name has up to three colon-separated parts:

```
[<section_spec> : <inheritance_list> : <punit_constraints>]
```

Only the first part is required; the inheritance list and punit constraints are optional.

## Section Specification (`section_spec`)

The first part identifies what is being configured:

- Domain default: `<domain_name>.default`
- Domain punit: `<domain_name>.<punit_kind>(<punit_range>)`
- Lexgion: `Lexgion(<code_ptr>)`

Examples:

```
[OpenMP.default]
[MPI.rank(0)]
[CUDA.device(0)]
[Lexgion(0x4010bd)]
```

## Inheritance List (`inheritance_list`)

The inheritance list is a comma-separated list of domain defaults that the current section inherits from.

Format:

```
<domain_name>.default, <domain_name>.default, ...
```

Example:

```
[Lexgion.default: OpenMP.default, MPI.default]
```

For lexgion sections, multiple domain defaults can be inherited.

## Punit Constraints (`punit_constraints`)

Constraints limit when a section applies based on punits from other domains. It is a comma-separated list.

Format:

```
<domain_name>.<punit_kind>(<punit_range>), ...
```

Example:

```
[OpenMP.thread(0-3): OpenMP.default: MPI.rank(0), CUDA.device(0)]
```

## Punit Range Syntax

A punit range can be:

- Single number: `5`
- Inclusive range: `0-4`
- List of numbers and ranges: `4, 6, 8-12, 14-16, 20-24`

Whitespace is allowed around commas and dashes.

## Section Content

### Domain Default Sections

A `<domain_name>.default` section defines default settings for a domain:

- Punit ranges for that domain using `<domain_name>.<punit_kind> = (<range>)`
- Event toggles for that domain using `on` or `off`

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

A punit section configures a subset of a domain's punits. It can inherit defaults and be constrained by other domains.

Example:

```ini
[OpenMP.thread(0-3): OpenMP.default: MPI.rank(0), CUDA.device(0)]
```

### Lexgion Sections

A lexgion section configures a code region identified by a code pointer. It may inherit from domain defaults and be constrained by punits.

Lexgion configuration keys include:

- `trace_starts_at`: invocation count to begin tracing
- `max_num_traces`: maximum number of traces to collect
- `tracing_rate`: trace 1 out of every N invocations

Lexgion sections may also enable or disable domain events using `DomainName.event = on|off`.

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

You can define default lexgion behavior using a lexgion default section.

Example:

```ini
[Lexgion.default: OpenMP.default, MPI.default]
    trace_starts_at = 0
    max_num_traces = 2000
    tracing_rate = 1
```

## Parsing and Data Mapping Notes

Implementation-related expectations implied by the example and comments:

- Parse comments, section headers, and key-value pairs.
- Use `<domain_name>.default` sections to populate the domain trace config table.
- Parse domain punit sections to populate per-punit configurations linked by a `next` pointer.
- Update domain punit ranges using `<domain_name>.<punit_kind> = (<range>)` entries found in domain defaults.
- Parse lexgion sections to populate the lexgion config table keyed by code pointer.
- Validate domain names and punit kinds against the domain info table before applying.

## Complete Example

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
