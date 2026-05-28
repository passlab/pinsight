# PInsight Python Tracing: Design and Implementation

> **Status**: Implemented, pending build/test on Linux with LTTng.
> **Requires**: Python 3.12+ (PEP 669 `sys.monitoring`)
> **Branch**: `master` (integrated from `feature/python-profiling-support`)

---

## 1. Overview

PInsight instruments Python applications using `sys.monitoring` (PEP 669, Python 3.12+), providing function-level and C-extension call tracing with full PInsight feature integration: 4-mode tracing, lexgion LRU + rate control, INTROSPECT cooperative pause, config file parsing, and SIGUSR1 reconfiguration.

The Python domain follows the same architecture as OpenMP (OMPT) and CUDA (CUPTI):

| Component | OpenMP | CUDA | Python |
|-----------|--------|------|--------|
| Interception API | OMPT | CUPTI | sys.monitoring |
| Callback source | `ompt_callback.c` | `cupti_callback.c` | `pysysmon_callback.c` |
| Tracepoint header | `ompt_lttng_ust_tracepoint.h` | `cupti_lttng_ust_tracepoint.h` | `pysysmon_lttng_ust_tracepoint.h` |
| LTTng provider | `ompt_pinsight_lttng_ust` | `cupti_pinsight_lttng_ust` | `pysysmon_pinsight_lttng_ust` |
| Domain header | `trace_domain_OpenMP.h` | `trace_domain_CUDA.h` | `trace_domain_Python.h` |

---

## 2. Architecture

```
┌──────────────────────────────────────────────────────────────┐
│  python3 -m pinsight my_app.py                               │
│                                                              │
│  pinsight.py  ─── activate() ──→ sys.monitoring              │
│    │                  registers PY_START, PY_RETURN,          │
│    │                  CALL, C_RETURN callbacks                │
│    ↓                                                         │
│  _pinsight_python.so (pysysmon_callback.c)                   │
│    │  on_py_start  ──→ lexgion_begin() + trace_bit           │
│    │  on_py_return ──→ lexgion_end()   + post_trace_update   │
│    │  on_call      ──→ piggyback parent trace_bit            │
│    │  on_c_return  ──→ piggyback parent trace_bit            │
│    ↓                                                         │
│  libpinsight.so (loaded via LD_PRELOAD)                      │
│    │  trace_config.c ──→ register_Python_trace_domain()      │
│    │  pinsight.c     ──→ lexgion LRU, rate control           │
│    │  control_thread ──→ mode transitions, SIGUSR1           │
│    ↓                                                         │
│  LTTng UST (pysysmon_pinsight_lttng_ust:*)                   │
└──────────────────────────────────────────────────────────────┘
```

### 2.1 Two Libraries, Two Load Paths

Python tracing uses **two separate shared libraries** unlike OpenMP/CUDA which embed everything in `libpinsight.so`:

| Library | Load mechanism | Contains |
|---------|---------------|----------|
| `libpinsight.so` | `LD_PRELOAD` | Core infrastructure (lexgion, control thread, domain registration). The Python domain is registered here via `register_Python_trace_domain()` in the `__attribute__((constructor))` path. |
| `_pinsight_python.so` | Python `import` | C extension with 4 sys.monitoring callbacks + LTTng tracepoint definitions. Links against `libpinsight.so` to call `lexgion_begin()`, `lexgion_end()`, etc. |

This split is required because:
1. Python's `import` mechanism needs a `PyInit__pinsight_python` entry point in a loadable `.so`
2. `libpinsight.so` is loaded via `LD_PRELOAD` before Python starts — it cannot depend on Python
3. The domain registration must happen in `libpinsight.so`'s constructor (before any callback fires)

### 2.2 Initialization Order

```
1. libpinsight.so loaded via LD_PRELOAD
   └── __attribute__((constructor(101))) initial_setup_trace_config()
       └── register_Python_trace_domain()          ← Python_domain_index assigned
           └── DSL expanded → domain_info_table populated

2. Python interpreter starts
   └── python3 -m pinsight my_app.py
       └── pinsight.py imports _pinsight_python
           └── PyInit__pinsight_python()            ← module loaded, domain_index valid
       └── pinsight.py calls activate()
           └── sys.monitoring.register_callback()   ← callbacks armed
           └── sys.monitoring.set_events()          ← events enabled

3. User script runs
   └── Every Python function call fires on_py_start → on_py_return
   └── Every C extension call fires on_call → on_c_return
```

---

## 3. Domain Registration (trace_domain_Python.h)

The Python domain uses the same DSL as OpenMP and CUDA, with `TRACE_EVENT_ID_INTERNAL` (dense, 0-based event IDs auto-assigned by the DSL loader).

### 3.1 Events (5)

Each `sys.monitoring` event gets its own event ID, consistent with how OpenMP uses separate IDs for `omp_parallel_begin` / `omp_parallel_end`:

| Event ID | Name | sys.monitoring event | Callback | Default |
|:--------:|------|---------------------|----------|---------|
| 0 | `pysysmon_py_start` | `PY_START` | `on_py_start` | ON |
| 1 | `pysysmon_py_return` | `PY_RETURN` | `on_py_return` | ON |
| 2 | `pysysmon_call` | `CALL` | `on_call` | ON |
| 3 | `pysysmon_c_return` | `C_RETURN` | `on_c_return` | ON |
| 4 | `pysysmon_import` | (reserved) | — | OFF |

### 3.2 Subdomains (3)

| Subdomain | Events | Purpose |
|-----------|--------|---------|
| `function` | `pysysmon_py_start`, `pysysmon_py_return` | Python function entry/exit |
| `bridge` | `pysysmon_call`, `pysysmon_c_return` | C extension call/return |
| `others` | `pysysmon_import` | Reserved for future use |

### 3.3 Punits (1)

| Punit | Range | ID function |
|-------|-------|-------------|
| `thread` | 0–255 | `pysysmon_get_thread_id()` |

The punit ID function returns a **domain-specific sequential thread ID** (0, 1, 2...) assigned per-thread via TLS + atomic counter, analogous to `omp_get_thread_num()`. This is separate from PInsight's internal `global_thread_num` (which uses 3000+ offsets to avoid collision with OpenMP 0+ and CUDA 2000+).

### 3.4 Starting Mode

`TRACING` — same as OpenMP and CUDA. Since Python tracing is opt-in (user must run `python3 -m pinsight`), the user has explicitly requested tracing.

---

## 4. Callback Implementation (pysysmon_callback.c)

### 4.1 Thread Model

Python threads in CPython are 1:1 mapped to OS threads (pthreads). Each OS thread has its own TLS (`__thread`) copy of `pinsight_thread_data`. Two separate ID schemes are used:

| ID | Variable | Assignment | Range | Purpose |
|----|----------|-----------|-------|---------|
| Domain thread ID | `pysysmon_thread_id` | Sequential atomic counter | 0, 1, 2... | Punit config filtering (user-facing) |
| PInsight thread ID | `global_thread_num` | Sequential atomic counter | 3000, 3001... | Trace record identification (internal) |

```c
// Domain-specific: punit config (analogous to omp_get_thread_num())
static _Atomic int pysysmon_thread_counter = 0;
static __thread int pysysmon_thread_id = -1;

// PInsight-internal: trace records (avoids collision with OpenMP 0+ / CUDA 2000+)
static _Atomic int pysysmon_global_thread_counter = 3000;
```

The first callback on any Python thread triggers `pysysmon_ensure_thread_init()`, which:
1. Assigns `global_thread_num = 3000 + N` via `init_thread_data()`
2. Assigns `pysysmon_thread_id = N` for punit filtering
3. Initializes the lexgion stack and LRU cache for this thread

### 4.2 PY_START Callback (on_py_start)

Called for every Python function entry:

```c
static PyObject *on_py_start(PyObject *self, PyObject *const *args, Py_ssize_t nargs) {
    // 1. Cooperative pause check (INTROSPECT support)
    pinsight_check_pause();

    // 2. Fast-path domain mode check (~2ns volatile read)
    if (!PINSIGHT_DOMAIN_ACTIVE(domain_default_trace_config[Python_domain_index].mode))
        return Py_None;

    // 3. Thread init (once per thread)
    pysysmon_ensure_thread_init();

    // 4. Lexgion begin — LRU lookup by PyCodeObject* pointer
    //    The code object pointer serves as the lexgion identity,
    //    analogous to codeptr_ra in OpenMP or __builtin_return_address in CUDA.
    const void *codeptr = (const void *)args[0];
    lexgion_record_t *record = lexgion_begin(PYTHON_LEXGION,
                                             PYSYSMON_EVENT_PY_START, codeptr);
    lexgion_t *lgp = record->lgp;

    // 5. Rate control + config resolution
    if (PINSIGHT_SHOULD_TRACE(Python_domain_index))
        lexgion_set_top_trace_bit_domain_event(lgp, Python_domain_index,
                                               PYSYSMON_EVENT_PY_START);

    // 6. Emit tracepoint if trace_bit is set
    if (PINSIGHT_SHOULD_TRACE(Python_domain_index) && lgp->trace_bit) {
        CodeInfo info;
        extract_code_info(code, &info);
        lttng_ust_tracepoint(pysysmon_pinsight_lttng_ust, function_begin, ...);
        free_code_info(&info);
    }
    return Py_None;
}
```

**Lexgion identity**: The `PyCodeObject*` pointer uniquely identifies each Python function definition. Every call to the same function uses the same pointer, so lexgion LRU lookup is O(1) on cache hit — exactly like `codeptr_ra` for OpenMP or `__builtin_return_address()` for CUDA.

**Lazy code info extraction**: Code info (`co_qualname`, `co_filename`, `co_firstlineno`) is only extracted when tracing is active (`trace_bit` is set). This avoids Python string operations on the fast path when tracing is suppressed by rate control.

### 4.3 PY_RETURN Callback (on_py_return)

Called for every Python function exit:

```c
static PyObject *on_py_return(...) {
    if (!PINSIGHT_DOMAIN_ACTIVE(...)) return Py_None;

    // Pop the lexgion stack
    lexgion_t *lgp = lexgion_end(NULL);
    if (!lgp) return Py_None;

    if (PINSIGHT_SHOULD_TRACE(Python_domain_index) && lgp->trace_bit) {
        // Emit function_end tracepoint
        lttng_ust_tracepoint(pysysmon_pinsight_lttng_ust, function_end, ...);

        // Rate-limit update + auto-trigger check
        lexgion_post_trace_update(lgp);
    }
    return Py_None;
}
```

The `lexgion_post_trace_update()` call handles:
- Incrementing the lexgion's trace counter
- Checking against `max_num_traces` for auto-trigger mode transitions
- Firing `pinsight_fire_mode_triggers()` when the threshold is reached

### 4.4 CALL / C_RETURN Callbacks (Bridge Events)

C extension calls (`numpy.linalg.solve()`, `mpi4py.MPI.Comm.Allreduce()`) are traced as "bridge" events — they represent transitions from Python to native code.

**Key design decision**: Bridge events do NOT create their own lexgions. They piggyback on the enclosing Python function's trace decision, identical to how OpenMP work regions piggyback on the enclosing parallel region. This avoids redundant lexgion creation and is semantically correct — the C call is part of the Python function's execution span.

```c
static PyObject *on_call(...) {
    PyObject *callable = args[2];

    // Only trace C function calls (not pure Python calls)
    if (PyCFunction_Check(callable) || Py_IS_TYPE(callable, &PyMethodDescr_Type)) {
        // Inherit trace decision from enclosing Python function
        lexgion_record_t *parent = pinsight_thread_data.current_record;
        if (parent && parent->lgp &&
            PINSIGHT_SHOULD_TRACE(Python_domain_index) && parent->lgp->trace_bit) {
            // Emit c_call_begin tracepoint
        }
    }
    return Py_None;
}
```

The `on_c_return` callback mirrors `on_call`, emitting `c_call_end`. Together they enable duration measurement for C extension calls.

### 4.5 Lexgion Stack Example

For a Python call `main() → compute() → numpy.linalg.solve()`:

```
Thread 0's lexgion stack:
  [0] main()     (PYTHON_LEXGION)  ← pushed by on_py_start (codeptr=main's PyCodeObject)
  [1] compute()  (PYTHON_LEXGION)  ← pushed by on_py_start
       on_call fires (c_call_begin)   ← NOT pushed (bridge event, piggybacks on compute)
       on_c_return fires (c_call_end) ← NOT pushed
  [1] compute()  (PYTHON_LEXGION)  ← popped by on_py_return
  [0] main()     (PYTHON_LEXGION)  ← popped by on_py_return
```

In a mixed Python+OpenMP+CUDA application, the lexgion stack correctly nests across domains:

```
Thread 0's lexgion stack:
  [0] parallel_begin  (OPENMP_LEXGION)   ← from OMPT
  [1] implicit_task   (OPENMP_LEXGION)   ← from OMPT
  [2] py_function     (PYTHON_LEXGION)   ← from on_py_start (embedded Python)
  [3] cudaLaunch      (CUDA_LEXGION)     ← from CUPTI callback
       popped cleanly in reverse order
```

---

## 5. LTTng Tracepoints (pysysmon_lttng_ust_tracepoint.h)

Provider: `pysysmon_pinsight_lttng_ust`

### 5.1 Tracepoint Definitions (4)

| Tracepoint | Fields | Fired by |
|------------|--------|----------|
| `function_begin` | `qualname`, `filename`, `lineno`, `code_id`, `python_thread_id` | `on_py_start` |
| `function_end` | `qualname`, `filename`, `lineno`, `code_id`, `python_thread_id` | `on_py_return` |
| `c_call_begin` | `caller_qualname`, `callee_name`, `caller_filename`, `caller_lineno`, `code_id` | `on_call` |
| `c_call_end` | `caller_qualname`, `callee_name`, `caller_filename`, `caller_lineno`, `code_id` | `on_c_return` |

### 5.2 Field Details

- `qualname`: Python function's qualified name (e.g., `MyClass.method`)
- `filename`: Source file path (e.g., `/app/solver.py`)
- `lineno`: First line number of the function definition
- `code_id`: `PyCodeObject*` cast to `unsigned long` (hex) — unique identifier for the function definition, also serves as the lexgion identity
- `python_thread_id`: Domain-specific thread ID (0, 1, 2...)
- `caller_qualname` / `callee_name`: For bridge events, the Python caller and C callee names

---

## 6. Python Launcher (pinsight.py)

The launcher handles `sys.monitoring` registration (Python-only API — no C equivalent):

```python
TOOL_ID = 3  # sys.monitoring tool slot

def activate(events=("function", "c_call")):
    sys.monitoring.use_tool_id(TOOL_ID, "pinsight")

    if "function" in events:
        sys.monitoring.register_callback(TOOL_ID, PY_START, on_py_start)
        sys.monitoring.register_callback(TOOL_ID, PY_RETURN, on_py_return)

    if "c_call" in events:
        sys.monitoring.register_callback(TOOL_ID, CALL, on_call)
        sys.monitoring.register_callback(TOOL_ID, C_RETURN, on_c_return)

    sys.monitoring.set_events(TOOL_ID, event_mask)
```

Usage:
```bash
# Zero-code-change tracing:
python3 -m pinsight my_app.py --arg1 --arg2

# Or programmatic:
import pinsight
pinsight.activate()
# ... your code ...
pinsight.deactivate()
```

The launcher uses `runpy.run_path()` to execute the target script as `__main__`, preserving `sys.argv` semantics.

---

## 7. Build Configuration

### 7.1 CMake

```cmake
option(PINSIGHT_PYTHON "Build with Python tracing support" FALSE)

# Must come after add_library(pinsight) since _pinsight_python links against it
if (PINSIGHT_PYTHON)
    find_package(Python3 COMPONENTS Interpreter Development REQUIRED)
    Python3_add_library(_pinsight_python MODULE src/pysysmon_callback.c)
    target_include_directories(_pinsight_python PRIVATE
        ${CMAKE_CURRENT_BINARY_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(_pinsight_python PRIVATE pinsight lttng-ust)

    configure_file(src/pinsight.py ${CMAKE_CURRENT_BINARY_DIR}/pinsight/__init__.py COPYONLY)
    configure_file(src/pinsight.py ${CMAKE_CURRENT_BINARY_DIR}/pinsight/__main__.py COPYONLY)
endif()
```

### 7.2 Build Commands

```bash
# Python-only build (disable other domains if not needed)
mkdir build && cd build
cmake -DPINSIGHT_PYTHON=TRUE \
      -DPINSIGHT_OPENMP=FALSE \
      -DPINSIGHT_MPI=FALSE \
      -DPINSIGHT_CUDA=FALSE ..
make

# Verify
ls -la libpinsight.so _pinsight_python*.so pinsight/
```

### 7.3 Build Outputs

| File | Description |
|------|-------------|
| `libpinsight.so` | Core library with Python domain registered |
| `_pinsight_python.cpython-3XX-*.so` | Python C extension (callbacks + tracepoints) |
| `pinsight/__init__.py` | Python package for `import pinsight` |
| `pinsight/__main__.py` | Python package for `python3 -m pinsight` |

---

## 8. Test and Verification

### 8.1 Quick Build Test

```bash
# Import test — verifies the C extension loads
PYTHONPATH=build python3 -c "import _pinsight_python; print('OK')"
```

### 8.2 LTTng Trace Test

```bash
# Start LTTng session daemon if not running
lttng-sessiond -d

# Create session and enable Python tracepoints
lttng create python-test
lttng enable-event -u "pysysmon_pinsight_lttng_ust:*"
lttng start

# Run the test program
LD_PRELOAD=build/libpinsight.so \
  PYTHONPATH=build \
  python3 -m pinsight test/python_simple/python_simple.py

# Stop and view
lttng stop
babeltrace2 ~/lttng-traces/python-test-* | head -30
lttng destroy
```

Expected output: `function_begin`/`function_end` pairs for `main`, `do_work`, and `c_call_begin`/`c_call_end` for `time.sleep`, `print`, etc.

### 8.3 Config File Test

Create `pinsight_trace_config.txt`:
```
[Python.global]
trace_mode = STANDBY
```

Run with `PINSIGHT_TRACE_CONFIG=pinsight_trace_config.txt` and verify no trace events are emitted.

### 8.4 Rate Limit Test

```bash
PINSIGHT_TRACE_RATE=0:10:1 \
  LD_PRELOAD=build/libpinsight.so \
  PYTHONPATH=build \
  python3 -m pinsight test/python_simple/python_simple.py
```

Verify only ~10 `function_begin`/`function_end` pairs are emitted per lexgion.

### 8.5 Environment Variable Control

```bash
# Disable Python tracing entirely
PINSIGHT_TRACE_PYTHON=OFF LD_PRELOAD=build/libpinsight.so ...

# Enable Python tracing (default: TRACING)
PINSIGHT_TRACE_PYTHON=TRACING LD_PRELOAD=build/libpinsight.so ...
```

### 8.6 SIGUSR1 Reconfiguration

```bash
# In one terminal, run a long script:
LD_PRELOAD=build/libpinsight.so PYTHONPATH=build python3 -m pinsight long_running.py &
PID=$!

# In another terminal, change mode:
echo "[Python.global]\ntrace_mode = STANDBY" > pinsight_trace_config.txt
kill -USR1 $PID

# Verify no new trace events are emitted after SIGUSR1
```

---

## 9. Source Files

| File | Role |
|------|------|
| `src/pysysmon_callback.c` | C extension: 4 sys.monitoring callbacks, thread init, domain globals |
| `src/pysysmon_lttng_ust_tracepoint.h` | LTTng UST tracepoint definitions (4 events) |
| `src/trace_domain_Python.h` | Domain registration DSL (5 events, 3 subdomains, 1 punit) |
| `src/pinsight.py` | Python launcher — sys.monitoring callback registration |
| `src/pinsight.h` | `PYTHON_LEXGION` enum value |
| `src/pinsight_config.h.cmake.in` | `#cmakedefine PINSIGHT_PYTHON` |
| `CMakeLists.txt` | `PINSIGHT_PYTHON` option + `_pinsight_python` module build |
| `src/trace_config.c` | `#include` + `register_Python_trace_domain()` call |
| `test/python_simple/python_simple.py` | Multi-threaded test program |
| `test/python_simple/Makefile` | LTTng session setup + test execution |

---

## 10. Design Decisions and Rationale

### 10.1 Why sys.monitoring (PEP 669) over sys.setprofile

| Feature | `sys.monitoring` (3.12+) | `sys.setprofile` (3.0+) |
|---------|-------------------------|------------------------|
| Overhead | ~4× lower (per-tool event masking) | Higher (global, all-or-nothing) |
| C extension calls | `CALL` + `C_RETURN` events | `c_call` + `c_return` |
| Per-code-object control | `set_local_events()` | Not available |
| Multiple tools | Yes (tool slots 0–5) | No (single profiler) |
| Adoption | Python 3.12+ (~20%+ of users) | Python 3.0+ |

Decision: **Python 3.12+ only**. The scientific Python stack (NumPy, SciPy) already requires 3.11+ per SPEC 0 policy. HPC environments predominantly run 3.12+. A `sys.setprofile` fallback can be added later if demand emerges.

### 10.2 Why Bridge Events Don't Create Lexgions

Creating a lexgion for every C extension call would:
1. Double the lexgion stack depth (every `numpy.add()` would push/pop)
2. Create thousands of lexgions for common C calls (print, len, etc.)
3. Conflict with the rate control model (C call traces would be independently rate-limited, losing the "how many traces per Python function" semantic)

Instead, bridge events inherit the enclosing Python function's `trace_bit`, matching the OpenMP pattern where work regions inherit from the parallel region.

### 10.3 Why Two Thread ID Schemes

| ID | Purpose | Who uses it |
|----|---------|-------------|
| `pysysmon_thread_id` (0, 1, 2...) | Config filtering: `[Python.thread(0-3)]` | User in config file |
| `global_thread_num` (3000, 3001...) | Trace record identification: stitch events into per-thread timelines | Analysis tools (TraceCompass) |

The offset scheme (OpenMP 0+, CUDA 2000+, Python 3000+) ensures `global_thread_num` is unique across all domains in the same process.

---

## 11. Known Limitations and Future Work

| Item | Status | Notes |
|------|--------|-------|
| `sys.setprofile` fallback | Deferred | For Python < 3.12 support; doubles testing surface |
| `pysysmon_import` event | Reserved (OFF) | Track import timing for startup analysis |
| Python GIL-aware tracing | Not implemented | Could skip tracing when waiting for GIL |
| `set_local_events()` | Not used | Per-code-object event masking for finer control |
| Multi-interpreter | Not tested | `sys.monitoring` is per-interpreter in 3.12+ |
| Install target | Not implemented | `_pinsight_python.so` and `pinsight/` package installation |
