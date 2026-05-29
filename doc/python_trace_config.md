# Python Trace Configuration

> **Status**: Domain-level and event-level config ready to use.
> Named lexgion matching: design proposed, implementation pending.

This document covers how to configure PInsight's Python tracing domain
(`pysysmon_pinsight_lttng_ust`) using the standard PInsight config file and
environment variables, and then proposes a Python-specific lexgion naming
extension for per-function trace control.

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

## Part 2 — Named Lexgion Matching (Design Proposal)

### 2.1 The Problem

For OpenMP and CUDA, lexgions are identified by `codeptr_ra` — a machine code
address that is stable across runs:

```ini
[Lexgion(0x4010bd)]           # always refers to the same parallel region
    max_num_traces = 50
```

For Python, the lexgion identity is `PyCodeObject*` — a **heap pointer** that:

1. Changes between runs (heap addresses are non-deterministic)
2. Is meaningless to the user (they think in `module.function` terms)
3. Cannot be known at config-write time

This means address-based lexgion config is **not usable for Python**. Users
need to say "trace `solver.compute` at rate 1, but trace `main` at rate 50"
using the function's qualified name.

### 2.2 Proposed Syntax

Extend the `Lexgion(...)` syntax with a `Python:` prefix that accepts qualified
function names as registered in `co_qualname`:

```ini
# Single function by qualified name
[Lexgion(Python:main)]
    max_num_traces = 200
    tracing_rate   = 1

# Nested function / method
[Lexgion(Python:MyClass.compute)]
    max_num_traces = 50
    tracing_rate   = 1

# Inner function
[Lexgion(Python:main.<locals>.do_work)]
    max_num_traces = 100
    tracing_rate   = 5

# Multiple functions sharing the same settings
[Lexgion(Python:solver.compute, Python:solver.setup)]
    max_num_traces = 200
    tracing_rate   = 1

# Module-qualified name (filename stem + qualname, for disambiguation)
[Lexgion(Python:solver.py:MyClass.compute)]
    max_num_traces = 50
```

Full config capabilities apply: rate triple, `trace_mode_after`, event
overrides, inheritance, and `REMOVE`:

```ini
# Rate limit the hot solver function
[Lexgion(Python:solver.compute)]: Python.default
    trace_starts_at  = 0
    max_num_traces   = 50
    tracing_rate     = 1
    trace_mode_after = Python:MONITORING

# Stop tracing a noisy utility function entirely
[REMOVE Lexgion(Python:utils.log_debug)]
```

### 2.3 How It Works — Implementation Design

#### 2.3.1 Config Parse Time

The parser detects the `Python:` prefix inside `Lexgion(...)`:

```c
// In parse_section_header(), inside the Lexgion(0x...) branch:
if (strncmp(trimmed, "Python:", 7) == 0) {
    // Named Python lexgion — store in a name-keyed table
    const char *qualname = trimmed + 7;
    lexgion_trace_config_t *lc = get_or_create_named_python_lexgion(qualname);
    current_lexgion_configs[num_current_lexgion_configs++] = lc;
} else {
    // Existing address-based path
    uint64_t addr = strtoull(trimmed, NULL, 0);
    ...
}
```

Named configs are stored in a separate hash table (keyed by `qualname` string),
distinct from the address-keyed `all_lexgion_trace_config[]` array:

```c
// New data structure in trace_config.h
#define MAX_NAMED_PYTHON_LEXGIONS 256

typedef struct {
    char qualname[256];             // co_qualname string key
    char filename_prefix[128];      // optional: "solver.py:" prefix for disambiguation
    lexgion_trace_config_t config;  // same config struct as address-based lexgions
} named_python_lexgion_t;

extern named_python_lexgion_t named_python_lexgion_table[MAX_NAMED_PYTHON_LEXGIONS];
extern int num_named_python_lexgions;
```

#### 2.3.2 Runtime Lookup in on_py_start

When a Python function is first encountered, after `lexgion_begin()` creates the
lexgion entry, the callback checks the named table and **binds** the name to the
`PyCodeObject*` address. Subsequent calls reuse the address-keyed entry directly
(O(1) — no string comparison in the hot path):

```c
static PyObject *on_py_start(PyObject *self, PyObject *const *args, Py_ssize_t nargs) {
    ...
    const void *codeptr = (const void *)args[0];
    lexgion_record_t *record = lexgion_begin(PYTHON_LEXGION, PYSYSMON_EVENT_PY_START, codeptr);
    lexgion_t *lgp = record->lgp;

    // First encounter for this PyCodeObject* — check named config table (once)
    if (!lgp->name_resolved) {
        pysysmon_resolve_named_config(lgp, args[0]);  // reads co_qualname, co_filename
        lgp->name_resolved = 1;
    }
    ...
}

// Resolution: called exactly once per unique PyCodeObject*
static void pysysmon_resolve_named_config(lexgion_t *lgp, PyObject *code) {
    PyObject *qualname_obj = PyObject_GetAttrString(code, "co_qualname");
    PyObject *filename_obj = PyObject_GetAttrString(code, "co_filename");
    if (!qualname_obj) { Py_XDECREF(filename_obj); return; }

    const char *qualname = PyUnicode_AsUTF8(qualname_obj);
    const char *filename = filename_obj ? PyUnicode_AsUTF8(filename_obj) : "";

    // Linear scan of named table (happens only once per unique function)
    for (int i = 0; i < num_named_python_lexgions; i++) {
        named_python_lexgion_t *entry = &named_python_lexgion_table[i];

        // Check filename prefix if specified
        if (entry->filename_prefix[0] != '\0') {
            const char *base = strrchr(filename, '/');
            base = base ? base + 1 : filename;
            if (strncmp(base, entry->filename_prefix,
                        strlen(entry->filename_prefix)) != 0)
                continue;
        }

        // Check qualname match
        if (strcmp(qualname, entry->qualname) == 0) {
            // Bind: copy named config into the lexgion's trace config
            lexgion_set_trace_config(lgp, Python_domain_index);
            *lgp->trace_config = entry->config;
            break;
        }
    }

    Py_XDECREF(qualname_obj);
    Py_XDECREF(filename_obj);
}
```

This keeps the hot path (every Python function call after the first) at the
same O(1) cost as today — a single `lgp->name_resolved` flag check.

#### 2.3.3 The `name_resolved` Flag

The `lexgion_t` struct needs one new field:

```c
// Addition to lexgion_t in pinsight.h
typedef struct lexgion {
    ...
    int name_resolved;    // 1 after pysysmon_resolve_named_config() has run
} lexgion_t;
```

Initialized to `0` by `lexgion_begin()` when a new lexgion is created. Reset
to `0` on config reload (SIGUSR1) so new named configs take effect immediately.

#### 2.3.4 Handling SIGUSR1 Config Reload

On reload, the named table is rebuilt from scratch (the file is re-parsed).
All `lexgion_t.name_resolved` flags are cleared so the next call to each
Python function re-checks the updated table:

```c
// In pinsight_load_trace_config() after re-parsing the file:
#ifdef PINSIGHT_PYTHON
    pysysmon_clear_name_resolved_flags();  // reset all lexgion_t.name_resolved = 0
#endif
```

### 2.4 Matching Semantics

| Config syntax | Matches |
|---------------|---------|
| `[Lexgion(Python:compute)]` | Any function whose `co_qualname == "compute"` in any file |
| `[Lexgion(Python:MyClass.compute)]` | `MyClass.compute` in any file |
| `[Lexgion(Python:solver.py:MyClass.compute)]` | `MyClass.compute` only in files named `solver.py` |
| `[Lexgion(Python:main.<locals>.do_work)]` | The `do_work` closure defined inside `main` |
| `[REMOVE Lexgion(Python:utils.log_debug)]` | Stops tracing `log_debug` in any file |

**Disambiguation rule**: If a qualname matches multiple files, the first match
in the named table wins. Add a `filename.py:` prefix to disambiguate.

**Priority order** (same as existing address-based logic):
1. Named Python lexgion match (if found)
2. `Lexgion(Python).default`
3. `Lexgion.default`

### 2.5 Files to Modify

| File | Change |
|------|--------|
| `src/trace_config.h` | Add `named_python_lexgion_t` struct, `named_python_lexgion_table[]`, `num_named_python_lexgions` |
| `src/trace_config.c` | Initialize and reset `named_python_lexgion_table` |
| `src/trace_config_parse.c` | Detect `Python:` prefix in `Lexgion(...)`, call `get_or_create_named_python_lexgion()` |
| `src/pinsight.h` | Add `name_resolved` flag to `lexgion_t` |
| `src/pinsight.c` | Reset `name_resolved = 0` in lexgion creation; clear all on SIGUSR1 reload |
| `src/pysysmon_callback.c` | Add `pysysmon_resolve_named_config()`, call from `on_py_start` |

### 2.6 Example: Full Python-Specific Config

```ini
# ── Domain-wide ─────────────────────────────────────────────────────────
[Python.global]
    trace_mode    = TRACING
    Python.thread = (0-7)      # 8-thread program

# ── Domain event defaults ────────────────────────────────────────────────
[Python.default]
    pysysmon_py_start  = on
    pysysmon_py_return = on
    pysysmon_c_start   = off   # disable C bridge by default
    pysysmon_c_return  = off

# ── Lexgion defaults ─────────────────────────────────────────────────────
[Lexgion(Python).default]: Python.default
    trace_starts_at = 0
    max_num_traces  = 50        # limit all functions to 50 traces
    tracing_rate    = 1
    trace_mode_after = Python:MONITORING  # auto-drop after enough data

# ── Named function overrides ─────────────────────────────────────────────

# Hot solver — full C bridge tracing, high rate limit
[Lexgion(Python:Solver.run)]: Python.default
    pysysmon_c_start  = on      # enable C bridge for this function
    pysysmon_c_return = on
    max_num_traces    = 200
    tracing_rate      = 1

# Initialization — trace only once (always same path, no value in repeating)
[Lexgion(Python:Solver.setup)]: Python.default
    max_num_traces = 1

# Noisy logging helper — skip entirely
[REMOVE Lexgion(Python:utils.log_debug)]

# Two functions with same settings — disambiguated by filename
[Lexgion(Python:solver_a.py:compute, Python:solver_b.py:compute)]: Python.default
    max_num_traces = 100
    tracing_rate   = 5
```

### 2.7 Limitations and Non-Goals

| Item | Notes |
|------|-------|
| Wildcard matching | `Python:Solver.*` to match all methods — deferred; increases parser complexity |
| Lambda tracing | Lambda `co_qualname` is `<lambda>` — not uniquely identifiable by name alone |
| Decorator wrappers | Wrapped functions may have their wrapper's `co_qualname` instead of the original |
| Pattern matching | No glob/regex — exact `co_qualname` match only in the initial implementation |
| Cross-run stability | Names are stable across runs; config files can be written once and reused |

---

## Summary

| Capability | Available Now | Future (Named Lexgion) |
|------------|:-------------:|:---------------------:|
| Domain ON/OFF/STANDBY | ✅ | ✅ |
| Event enable/disable (c_start, py_start…) | ✅ | ✅ |
| Per-thread filtering | ✅ | ✅ |
| Rate control (all functions) | ✅ | ✅ |
| INTROSPECT auto-pause | ✅ | ✅ |
| SIGUSR1 reconfiguration | ✅ | ✅ |
| Rate control **per named function** | ❌ | ✅ |
| Enable C bridge for **one function only** | ❌ | ✅ |
| Skip tracing a specific function | ❌ | ✅ |
