# Python Trace Configuration

> **Status**: Fully implemented — domain-level, event-level, named lexgion matching, and thread/punit filtering are all ready to use.

This document covers how to configure PInsight's Python tracing domain
(`pysysmon_pinsight_lttng_ust`) using the standard PInsight config file and
environment variables.

For the general config file format, see [PINSIGHT_TRACE_CONFIG_FORMAT.md](PINSIGHT_TRACE_CONFIG_FORMAT.md).
For Python tracing implementation details, see [python_tracing_implementation.md](python_tracing_implementation.md).

---

## Part 1 — What Works Today (No Code Changes Needed)

The PInsight configuration engine is fully domain-agnostic. Because the Python
domain is registered via the same DSL as OpenMP and CUDA, all existing config
mechanisms work for Python out of the box.

### 1.1 Environment Variables

#### Domain mode

```bash
# Disable Python tracing entirely (zero overhead)
export PINSIGHT_TRACE_PYTHON=OFF

# Enable Python tracing (default when PINSIGHT_PYTHON=TRUE)
export PINSIGHT_TRACE_PYTHON=TRACING

# Bookkeeping only — lexgion LRU runs but no LTTng output
export PINSIGHT_TRACE_PYTHON=MONITORING
```

#### Rate-based sampling (applies to all Python lexgions)

```bash
# Skip first 10 calls per function, then trace 1-in-50, stop after 100 traces
export PINSIGHT_TRACE_RATE=10:100:50

# Trace first 100 calls per function, then switch Python to MONITORING
export PINSIGHT_TRACE_RATE=0:100:1:Python:MONITORING
```

### 1.2 Domain-Wide Mode and Punit Scope

Control the Python domain globally and restrict which Python threads are traced:

```ini
[Python.global]
    trace_mode = TRACING
    Python.thread = (0-3)     # only trace Python threads 0, 1, 2, 3
```

```ini
# Disable all Python tracing at runtime (send kill -USR1 <pid> after writing this)
[Python.global]
    trace_mode = OFF
```

```ini
# Near-zero overhead — callbacks fire but return immediately; recoverable via SIGUSR1
[Python.global]
    trace_mode = STANDBY
```

### 1.3 Domain Default Event Selection

Choose which event categories to trace:

```ini
[Python.default]
    pysysmon_py_start  = on    # Python function entry (default: on)
    pysysmon_py_return = on    # Python function exit  (default: on)
    pysysmon_c_start   = off   # C extension call begin (turn off to reduce volume)
    pysysmon_c_return  = off   # C extension call end
    pysysmon_import    = off   # module import (reserved, default: off)
```

**Common pattern** — Python functions only, no C bridge noise:

```ini
[Python.default]
    pysysmon_c_start  = off
    pysysmon_c_return = off
```

**C extension calls only** — useful for tracing numpy/scipy overhead without Python call stack noise:

```ini
[Python.default]
    pysysmon_py_start  = off
    pysysmon_py_return = off
    pysysmon_c_start   = on
    pysysmon_c_return  = on
```

### 1.4 Per-Thread Configuration

Trace only specific Python threads (thread IDs are assigned 0-N by
`pysysmon_get_thread_id()`, matching the order threads first call into Python):

```ini
# Trace only the main thread (thread 0) and the first worker (thread 1)
[Python.thread(0-1)]: Python.default
```

```ini
# Trace all threads except thread 0 (main thread — often just coordination code)
[Python.thread(1-255)]: Python.default
```

```ini
# Remove thread-specific config (threads fall back to Python.default)
[REMOVE Python.thread(0-1)]
```

### 1.5 Default Lexgion Rate Control

Apply rate-limited sampling to all Python function calls (all lexgions):

```ini
# Trace at most 200 calls per function, at 1-in-10 rate
[Lexgion(Python).default]
    trace_starts_at = 0
    max_num_traces  = 200
    tracing_rate    = 10
    pysysmon_py_start  = on
    pysysmon_py_return = on
    pysysmon_c_start   = off
    pysysmon_c_return  = off
```

```ini
# Warm-up skip: ignore first 50 calls per function, then trace 100 at rate 1
[Lexgion(Python).default]
    trace_starts_at = 50
    max_num_traces  = 100
    tracing_rate    = 1
```

### 1.6 Auto Mode Switch After Tracing

Automatically drop to MONITORING (near-zero overhead) after collecting enough data:

```ini
[Lexgion(Python).default]
    max_num_traces  = 100
    tracing_rate    = 1
    trace_mode_after = MONITORING
```

Switch Python to MONITORING but keep OpenMP in TRACING:

```ini
[Lexgion(Python).default]
    max_num_traces   = 200
    trace_mode_after = Python:MONITORING, OpenMP:TRACING
```

### 1.7 INTROSPECT Workflow

Collect 50 traces per Python function, then pause the application and run an
analysis script (e.g., detect hotspots), then resume in TRACING mode:

```ini
[Lexgion(Python).default]
    max_num_traces   = 50
    tracing_rate     = 1
    trace_mode_after = INTROSPECT:60:analyze_python_traces.sh:TRACING
```

The script receives `<chunk_path> <app_pid> <config_file>` as arguments and
can use `kill -USR1 <app_pid>` to resume early.

### 1.8 Runtime Reconfiguration via SIGUSR1

All of the above can be changed while the application is running:

```bash
# Application is running with TRACING...

# 1. Edit the config file
cat > pinsight_trace_config.txt << 'EOF'
[Python.global]
    trace_mode = STANDBY

[Lexgion(Python).default]
    max_num_traces = 500
    tracing_rate   = 20
EOF

# 2. Signal the application to reload
kill -USR1 $APP_PID
```

The reload is applied at the next Python callback entry — no restart needed,
no trace data is lost.

### 1.9 Combined Multi-Domain Example

Mixed Python + OpenMP application. Trace Python main thread only, with rate
limiting; keep OpenMP at full tracing:

```ini
[Python.global]
    trace_mode    = TRACING
    Python.thread = (0)         # main thread only

[OpenMP.global]
    trace_mode       = TRACING
    OpenMP.thread    = (0-15)

[Python.default]
    pysysmon_py_start  = on
    pysysmon_py_return = on
    pysysmon_c_start   = off    # skip C bridges to reduce volume
    pysysmon_c_return  = off

[Lexgion(Python).default]: Python.default
    trace_starts_at = 0
    max_num_traces  = 100
    tracing_rate    = 5
    trace_mode_after = Python:MONITORING

[Lexgion(OpenMP).default]: OpenMP.default
    max_num_traces  = 200
    tracing_rate    = 1
```

---

## Part 2 — Named Lexgion Matching

For OpenMP and CUDA, lexgions are identified by `codeptr_ra` — a machine code
address stable across runs. For Python, the lexgion identity is `PyCodeObject*` —
a heap pointer that changes each run and is meaningless to users. Named lexgion
config lets you identify Python functions by their qualified name (`co_qualname`).

### 2.1 Syntax

```ini
# Single function by qualified name
[Lexgion(Python:main)]
    max_num_traces = 200
    tracing_rate   = 1

# Nested function / method
[Lexgion(Python:MyClass.compute)]
    max_num_traces = 50
    tracing_rate   = 1

# Inner function (closure)
[Lexgion(Python:main.<locals>.do_work)]
    max_num_traces = 100
    tracing_rate   = 5

# Multiple functions sharing the same settings
[Lexgion(Python:solver.compute, Python:solver.setup)]
    max_num_traces = 200
    tracing_rate   = 1

# Disambiguate by filename stem (when same qualname appears in multiple files)
[Lexgion(Python:solver.py:MyClass.compute)]
    max_num_traces = 50
```

Inheritance and rate control work the same as address-based lexgions:

```ini
# Rate limit the hot solver function
[Lexgion(Python:solver.compute)]: Python.default
    trace_starts_at  = 0
    max_num_traces   = 50
    tracing_rate     = 1
    trace_mode_after = Python:MONITORING

# Stop tracing a noisy utility function entirely
[Lexgion(Python:utils.log_debug)]: Python.default
    max_num_traces = 0
```

### 2.2 Matching Semantics

| Config syntax | Matches |
|---------------|---------|
| `[Lexgion(Python:compute)]` | Any function whose `co_qualname == "compute"` in any file |
| `[Lexgion(Python:MyClass.compute)]` | `MyClass.compute` in any file |
| `[Lexgion(Python:solver.py:MyClass.compute)]` | `MyClass.compute` only in files named `solver.py` |
| `[Lexgion(Python:main.<locals>.do_work)]` | The `do_work` closure defined inside `main` |

**Priority order**:
1. Named Python lexgion match (if found)
2. `[Lexgion(Python).default]`
3. `[Lexgion.default]`

**Config reload**: On SIGUSR1, names are re-resolved. Each function is re-matched
against the updated named config table on its next call. This uses the
`name_resolved_gen` generation counter in `lexgion_t` — no flag-clearing loop
needed; stale entries are detected lazily per-function.

### 2.3 How It Works (Implementation)

At `on_py_start`, the name is resolved once per unique function per config-reload
cycle. The key check is:

```c
if (lgp->name_resolved_gen != trace_config_change_counter) {
    // Extract co_qualname and co_filename (new references, safe to DECREF after storing)
    // Store lgp->name (UTF-8 ptr) and lgp->filename_hint (basename)
    // Set lgp->name_resolved_gen = trace_config_change_counter
    // Force config re-resolve: lgp->trace_config_change_counter = (unsigned int)-1
}
```

The stored `lgp->name` is used by `lexgion_set_top_trace_bit_domain_event()` to
look up named lexgion configs. After the first call, subsequent calls are O(1) —
no string operations unless the config is reloaded.

**String lifetime**: `PyUnicode_AsUTF8()` returns a pointer into the Python
object's internal buffer. The `PyCodeObject` (and thus its `co_qualname` /
`co_filename` string objects) lives for the application's lifetime — the internal
UTF-8 buffer is valid permanently. The new references from `PyObject_GetAttrString`
are `Py_XDECREF`-ed immediately after storing the pointer; the parent code object
retains ownership.

### 2.4 Example: Full Named Config

```ini
# ── Domain event defaults ────────────────────────────────────────────────
[Python.default]
    pysysmon_py_start  = on
    pysysmon_py_return = on
    pysysmon_c_start   = off   # disable C bridge by default
    pysysmon_c_return  = off

# ── Lexgion defaults ─────────────────────────────────────────────────────
[Lexgion(Python).default]: Python.default
    trace_starts_at = 0
    max_num_traces  = 50
    tracing_rate    = 1
    trace_mode_after = Python:MONITORING

# ── Named function overrides ─────────────────────────────────────────────

# Hot solver — full C bridge tracing, high rate limit
[Lexgion(Python:Solver.run)]: Python.default
    pysysmon_c_start  = on
    pysysmon_c_return = on
    max_num_traces    = 200
    tracing_rate      = 1

# Initialization — trace only once
[Lexgion(Python:Solver.setup)]: Python.default
    max_num_traces = 1

# Suppress a noisy helper
[Lexgion(Python:utils.log_debug)]: Python.default
    max_num_traces = 0

# Two functions with same settings, disambiguated by filename
[Lexgion(Python:solver_a.py:compute, Python:solver_b.py:compute)]: Python.default
    max_num_traces = 100
    tracing_rate   = 5
```

### 2.5 Limitations

| Item | Notes |
|------|-------|
| Wildcard matching | `Python:Solver.*` — deferred |
| Lambda tracing | Lambda `co_qualname` is `<lambda>` — not uniquely identifiable by name |
| Decorator wrappers | May show wrapper's `co_qualname` instead of original |
| Pattern matching | Exact `co_qualname` match only |

---

## Part 3 — Thread/Punit Filtering

Restrict tracing to specific Python threads by appending a punit constraint
to a named or default lexgion config section:

### 3.1 Syntax

```ini
[Lexgion(Python:funcname)] : Python.default : Python.thread(N-M)
    max_num_traces = K
```

The `: Python.default` inherits domain event defaults. The `: Python.thread(N-M)`
restricts this config entry to Python threads whose `pysysmon_get_thread_id()`
returns a value in the range `[N, M]`.

Thread IDs are assigned sequentially at first callback:
- **Thread 0**: main Python thread (assigned before any `threading.Thread` is started)
- **Threads 1-N**: worker threads (assigned at their first `on_py_start`)

### 3.2 Examples

```ini
# Suppress all Python tracing by default, then selectively enable:
[Lexgion(Python).default]
    max_num_traces = 0

# Trace "worker" function only on threads 1 and 2 (max 8 traces each)
[Lexgion(Python:worker)] : Python.default : Python.thread(1-2)
    max_num_traces = 8

# Trace "main_work" function only on the main thread (thread 0)
[Lexgion(Python:main_work)] : Python.default : Python.thread(0)
    max_num_traces = 5
```

```ini
# Trace worker on threads 3 and 4 only
[Lexgion(Python).default]
    max_num_traces = 0

[Lexgion(Python:worker)] : Python.default : Python.thread(3-4)
    max_num_traces = 6
```

```ini
# All 4 worker threads traced, max 5 per thread
[Lexgion(Python).default]
    max_num_traces = 0

[Lexgion(Python:worker)] : Python.default : Python.thread(1-4)
    max_num_traces = 5
```

### 3.3 How It Works

The punit filter is checked inside `lexgion_set_top_trace_bit_domain_event()`.
When a lexgion config entry has `domain_punit_set_set = 1`, the function calls
`domain_punit_set_match(domain_punits, domain_index)` which reads the current
thread's punit ID (via `pysysmon_get_thread_id()`) and checks the bitset.
If the current thread ID is not in the set, `trace_bit` is cleared.

The punit filter is stored per lexgion config entry, allowing different
thread ranges for different named functions simultaneously.

### 3.4 Combining Thread Filter with Other Config

Named lexgion config, thread filter, rate control, and auto-trigger all compose:

```ini
[Lexgion(Python).default]
    max_num_traces = 0      # suppress all functions by default

# Worker traced only on threads 1-2, max 7 each, auto-drop after
[Lexgion(Python:worker)] : Python.default : Python.thread(1-2)
    max_num_traces   = 7
    trace_mode_after = Python:MONITORING

# Main thread work, limit 5 traces, then INTROSPECT
[Lexgion(Python:main_work)] : Python.default : Python.thread(0)
    max_num_traces   = 5
    trace_mode_after = INTROSPECT:30:analyze.sh:TRACING
```

---

## Summary

| Capability | Status |
|------------|:------:|
| Domain ON/OFF/STANDBY/MONITORING | ✅ |
| Event enable/disable (`c_start`, `py_start`…) | ✅ |
| Per-thread filtering (domain-level) | ✅ |
| Rate control (all functions) | ✅ |
| INTROSPECT auto-pause | ✅ |
| SIGUSR1 reconfiguration | ✅ |
| Rate control per named function | ✅ |
| Enable C bridge for one function only | ✅ |
| Suppress tracing for a specific function | ✅ |
| Thread filter per named lexgion | ✅ |
| Wildcard/regex name matching | Deferred |
