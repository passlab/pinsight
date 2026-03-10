# PInsight Trace Configuration and Reconfiguration Design

## 1. Motivation and Goals

Runtime configuration and reconfiguration of tracing is critical to enable dynamic, region-specific, and **low-overhead** tracing. The design is guided by these principles:

1. **Minimal overhead when not tracing** — When a domain is set to OFF, all its callbacks should be deregistered at the runtime level (e.g., `ompt_set_callback(event, NULL)`) so the runtime never dispatches events. This achieves true zero per-event overhead, not just an early-return guard.

2. **Selective tracing** — Users should be able to control tracing at multiple granularities: per-domain, per-event, per-parallel-unit (thread/rank/device), and per-code-region (lexgion). Tracing only what matters dramatically reduces both I/O and computation overhead.

3. **Rate-limited tracing** — For long-running applications, tracing every execution of a hot code region produces redundant data. Rate-based sampling (trace 1-in-N executions) controls data volume while preserving statistical fidelity.

4. **Runtime reconfiguration without restart** — Users should be able to change tracing configuration while the application is running, enabling workflows such as starting with tracing off during warm-up, then enabling it for the region of interest.

5. **Lazy configuration resolution** — Per-thread configuration lookups use a cached pointer validated against a global `trace_config_change_counter`. Threads only re-resolve their configuration when the counter indicates a change, avoiding synchronization overhead on every event.

## 2. Configuration Scopes

Configuration is organized in four scopes from coarsest to finest:

| Scope | What it controls | Options |
|-------|-----------------|---------|
| **Domain** | Entire tracing subsystem (OpenMP, MPI, CUDA) | `trace_mode`: OFF / MONITORING / TRACING |
| **Event** | Individual event types within a domain | on / off per event |
| **Punit** | Parallel unit subsets (threads, ranks, devices) | Include/exclude specific IDs |
| **Lexgion** | Specific code regions by address | Rate triple (`trace_starts_at`, `max_num_traces`, `tracing_rate`), event overrides |

The checking order inside a tracing callback is: domain mode → punit match → event enabled → lexgion rate decision. Each level provides an early-exit opportunity to skip unnecessary work.

## 3. Launch-Time Configuration

At process launch, PInsight reads configuration from two sources. Environment variables are applied first, and the config file (if present) can override or extend them.

### 3.1 Environment Variables

#### Domain Trace Mode

```bash
export PINSIGHT_TRACE_OPENMP=OFF|MONITORING|TRACING
export PINSIGHT_TRACE_MPI=OFF|MONITORING|TRACING
export PINSIGHT_TRACE_CUDA=OFF|MONITORING|TRACING
```

Each variable sets the `trace_mode` for the corresponding domain. Accepted values: `OFF`, `FALSE`, `0` (→ OFF); `MONITORING`, `MONITOR` (→ MONITORING); `ON`, `TRUE`, `TRACING`, `1` (→ TRACING). If unset, the default is TRACING for domains with registered events, OFF otherwise.

#### Rate-Based Sampling

```bash
export PINSIGHT_TRACE_RATE=trace_starts_at:max_num_traces:tracing_rate[:mode_after]
```

Sets the default sampling triple for **all** lexgions:
- `trace_starts_at` — Skip this many executions before starting to trace (default: 0)
- `max_num_traces` — Stop tracing after this many traces; -1 = unlimited (default: -1)
- `tracing_rate` — Trace 1 out of every N executions (default: 1 = trace every execution)
- `mode_after` (optional) — Automatically switch domain trace modes when `max_num_traces` is reached. Formats:
  - Shorthand: `MONITORING` (switch all domains with events)
  - Per-domain: `OpenMP=MONITORING,MPI=OFF` (comma-separated, `=` separates domain from mode)

Examples:
- `PINSIGHT_TRACE_RATE=10:100:50` — skip the first 10 executions, then trace 1-in-50, stop after 100 traces total.
- `PINSIGHT_TRACE_RATE=0:100:1:MONITORING` — trace first 100 executions, then switch all domains to MONITORING.
- `PINSIGHT_TRACE_RATE=0:100:1:OpenMP=MONITORING,MPI=OFF` — trace first 100, then switch OpenMP to MONITORING and MPI to OFF.

> **Note:** Environment variables are read once at process launch. They cannot be changed from outside a running process. For runtime reconfiguration, use the config file.

### 3.2 Configuration File

The config file provides fine-grained control beyond what env vars offer: per-event toggling, per-punit filtering, and per-lexgion rate control.

PInsight looks for the config file in this order:
1. Path specified by `PINSIGHT_TRACE_CONFIG_FILE` environment variable
2. Default fallback: `pinsight_trace_config.txt` in the current working directory

If neither exists, PInsight runs with defaults. The fallback enables runtime reconfiguration without requiring the env var to be set at launch.

The config file uses an enhanced INI-style format with three types of domain sections and three types of lexgion sections:

#### Domain Sections

| Section | Purpose | Key-Value Pairs |
|---------|---------|-----------------|
| `[Domain.global]` | Domain-wide structural settings | `trace_mode = OFF\|MONITORING\|TRACING`, `Domain.PunitKind = (Range)` |
| `[Domain.default]` | Default event configuration | `EventName = on\|off` |
| `[Domain.PunitKind(Set)]` | Punit-specific event overrides | `EventName = on\|off` |

#### Lexgion Sections

| Section | Purpose | Key-Value Pairs |
|---------|---------|-----------------|
| `[Lexgion.default]` | Default configuration for all code regions | Rate triple, `trace_mode_after`, event overrides |
| `[Lexgion(Domain).default]` | Default for a specific domain's regions | Rate triple, `trace_mode_after`, event overrides |
| `[Lexgion(Address)]` | Configuration for specific code regions | Rate triple, `trace_mode_after`, event overrides |

Example config file:
```ini
# Domain-wide: set mode and punit scope
[OpenMP.global]
    trace_mode = TRACING
    OpenMP.thread = (0-7)

# Domain defaults: event selection
[OpenMP.default]
    omp_task_create = off
    omp_task_schedule = off

# Lexgion defaults: rate-limited sampling with auto mode switch
[Lexgion.default]: OpenMP.default
    tracing_rate = 50
    max_num_traces = 100
    trace_mode_after = MONITORING

# Specific code region: custom rate
[Lexgion(0x4010bd)]: OpenMP.default
    tracing_rate = 10
    max_num_traces = 200
```

See [PINSIGHT_TRACE_CONFIG_FORMAT.md](PINSIGHT_TRACE_CONFIG_FORMAT.md) for the complete format specification including actions (SET/RESET/REMOVE), inheritance, and punit constraints.

## 4. Runtime Reconfiguration

### 4.1 Mechanism: SIGUSR1

Runtime reconfiguration is user-initiated via the SIGUSR1 signal. This is an explicit, intentional action — PInsight never automatically re-reads the config file during normal execution.

### 4.2 Procedure

```
┌─────────────┐        ┌──────────────────────┐        ┌──────────────┐
│  User edits  │  ───▶  │  kill -USR1 <pid>     │  ───▶  │  PInsight    │
│  config file │        │  (signal the process) │        │  reloads     │
└─────────────┘        └──────────────────────┘        └──────────────┘
```

1. **Edit the config file** — Modify `pinsight_trace_config.txt` (or the file pointed to by `PINSIGHT_TRACE_CONFIG_FILE`). For example, change `trace_mode = TRACING` to `trace_mode = OFF`.

2. **Send the signal** — `kill -USR1 <pid>` to the running application.

3. **PInsight reloads** — The SIGUSR1 handler sets a flag (`config_reload_requested`). At the next safe point, PInsight:
   - Calls `pinsight_load_trace_config()` which checks the file's mtime. If changed, it re-parses the file and increments `trace_config_change_counter`.
   - Calls `pinsight_register_openmp_callbacks()` (and equivalent functions for other domains) to register/deregister callbacks based on the new mode.
   - Each thread lazily re-resolves its cached `lexgion_trace_config` pointer when it detects the counter has changed.

### 4.3 What Can Be Reconfigured at Runtime

| Setting | Reconfigurable? | Notes |
|---------|----------------|-------|
| Domain `trace_mode` | ✅ | Via `[Domain.global]` trace_mode key |
| Domain event enable/disable | ✅ | Via `[Domain.default]` event keys |
| Punit ranges | ✅ | Via `[Domain.global]` or `[Domain.PunitKind(Set)]` |
| Lexgion rate triple | ✅ | Via `[Lexgion(Address)]` section |
| Lexgion event overrides | ✅ | Via `[Lexgion(Address)]` event keys |
| `trace_mode_after` triggers | ✅ | Via `[Lexgion(*)]` sections; `auto_triggered` flags reset on reload |
| RESET / REMOVE configs | ✅ | Via `[RESET ...]` and `[REMOVE ...]` actions |

### 4.4 Example Workflows

**Warm-up then trace:**
```bash
# Launch with tracing off
OMP_TOOL_LIBRARIES=libpinsight.so ./myapp &
APP_PID=$!

# After warm-up, enable tracing
echo '[OpenMP.global]
    trace_mode = TRACING' > pinsight_trace_config.txt
kill -USR1 $APP_PID
```

**Trace then reduce overhead:**
```bash
# Application running with full tracing...

# Switch to monitoring-only (bookkeeping, no LTTng output)
echo '[OpenMP.global]
    trace_mode = MONITORING' > pinsight_trace_config.txt
kill -USR1 $APP_PID
```

**Disable all tracing (zero overhead):**
```bash
echo '[OpenMP.global]
    trace_mode = OFF
[MPI.global]
    trace_mode = OFF' > pinsight_trace_config.txt
kill -USR1 $APP_PID
```

## 5. Low-Overhead Design Considerations

### 5.1 Domain OFF = Zero Per-Event Overhead

When a domain's mode is OFF, its callbacks are deregistered from the runtime (e.g., `ompt_set_callback(event, NULL)` for OpenMP). The runtime itself stops dispatching events — there is no PInsight code in the hot path at all.

### 5.2 Lazy Configuration Resolution

Each thread caches its resolved `lexgion_trace_config` pointer. On every event, the thread compares a local counter against the global `trace_config_change_counter`. If they match, the cached pointer is used directly (one integer comparison). Only when the counter changes (due to config file reload) does the thread re-resolve its configuration through the lookup hierarchy.

### 5.3 Hierarchical Lookup with Early Exit

The 3-level configuration lookup (address-specific → domain-specific → global default) is designed so that the common case (no per-address config) resolves in O(1). The checking order within a callback (domain mode → event enabled → punit match → lexgion rate) provides multiple early-exit points.

### 5.4 Rate-Based Sampling

For long-running applications, the rate triple (`trace_starts_at`, `max_num_traces`, `tracing_rate`) reduces trace volume by orders of magnitude. A region executed 10,000 times with `tracing_rate=100, max_num_traces=50` produces only 50 traces instead of 10,000 — a 200× reduction in tracing I/O.

### 5.5 Nesting Optimization

Common nested regions (work, masked) piggyback on the parent region's tracing decision, avoiding redundant lookup and bookkeeping overhead. This is implemented via thread-local state (`enclosing_work_lgp`) rather than per-event configuration checks.

### 5.6 Automatic Mode Switching (`trace_mode_after`)

The auto-trigger mechanism enables automatic domain mode transitions without manual intervention. It is built on the same deferred re-registration pattern as SIGUSR1 reloads:

1. **Trigger condition**: Inside `lexgion_post_trace_update()`, after incrementing `trace_counter`, the code checks if `trace_counter >= max_num_traces` and `num_mode_triggers > 0`.

2. **Firing**: `pinsight_fire_mode_triggers(trace_config)` iterates over the configured triggers:
   - For **shorthand triggers** (`domain_idx == -1`): all domains that have events associated with the lexgion (`domain_events[j].set == 1`) are switched to the target mode.
   - For **explicit triggers** (`domain_idx >= 0`): only the specified domain is switched.
   - Each domain's `auto_triggered` flag is set to prevent repeated mode switches from subsequent lexgions.

3. **Deferred re-registration**: After firing, `mode_change_requested` is set (similar to `config_reload_requested`). At the next safe point in `lexgion_set_top_trace_bit_domain_event()`, this flag is consumed via atomic exchange, and `pinsight_register_openmp_callbacks()` is called to register/deregister callbacks based on the new modes.

4. **Reset**: Sending `SIGUSR1` to reload the config file resets all `auto_triggered` flags, allowing triggers to fire again with the new configuration.

Key data structures:
- `trace_mode_trigger_t { int domain_idx; pinsight_domain_mode_t mode; }` — stored in `lexgion_trace_config_t.mode_triggers[]`
- `domain_trace_config_t.auto_triggered` — per-domain flag preventing re-triggering
- `volatile sig_atomic_t mode_change_requested` — global flag for deferred callback re-registration

## 6. Related Documentation

- [PINSIGHT_TRACE_CONFIG_FORMAT.md](PINSIGHT_TRACE_CONFIG_FORMAT.md) — Config file syntax specification
- [trace_config_example.txt](trace_config_example.txt) — Example config file
- [domain_trace_modes.md](domain_trace_modes.md) — Detailed mode descriptions and benchmark data
