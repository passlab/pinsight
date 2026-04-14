# PInsight Python Tracing Support: Design and Implementation Plan

**Date:** 2026-04-14  
**Status:** Design  
**Author:** Y. Yan  

---

## 1. Motivation

HPC workloads increasingly use Python as the orchestration layer — NumPy, SciPy, CuPy,
PyTorch, and mpi4py all delegate heavy computation to C/C++/CUDA libraries that PInsight
already traces via OMPT, PMPI, and CUPTI. However, the **Python call layer** that dispatches
these operations is invisible in PInsight traces. Adding Python tracing enables:

1. **Cross-domain correlation**: See the Python function that triggered an OpenMP parallel
   region, MPI collective, or CUDA kernel launch on a single unified LTTng timeline.
2. **Python-level bottleneck detection**: Identify Python overhead (GIL contention, object
   allocation, import latency) interleaved with native computation.
3. **HPC workflow tracing**: Trace driver scripts that orchestrate multi-physics simulations,
   ML training loops, or reduction pipelines.

### Target Use Cases

```
Python: numpy.linalg.solve()          ← python_function_begin
  └→ OpenMP: parallel region          ← ompt_parallel_begin
       └→ barrier wait                ← ompt_sync_wait
Python: comm.Allreduce(...)           ← python_function_begin
  └→ MPI: Allreduce                   ← mpi_allreduce_begin
Python: model.forward(x)             ← python_function_begin
  └→ CUDA: cublasSgemm                ← cuda_kernel_launch
```

---

## 2. Design Options Analysis

### 2.1 Python Instrumentation Mechanisms

| Approach | Overhead | Granularity | C access | Python version | PInsight fit |
|----------|----------|-------------|----------|----------------|-------------|
| **sys.monitoring** (PEP 669) | Low (per-event) | CALL/RETURN/LINE selectable | PyCFunction | 3.12+ | ⭐ Best |
| **sys.setprofile** | Medium | call/return only | PyCFunction | 3.x | Good fallback |
| **PyEval_SetProfile** | Low (pure C) | call/return only | Direct C pointer | 3.x | Fastest, but less portable |
| **Modify cProfile** | Medium | call/return | Direct C (forks CPython) | 3.x | Fragile |
| **sys.settrace** | High (per-line) | line+call+return | PyCFunction | 3.x | Too slow |
| **CPython DTrace probes** | Low | Fixed set | Kernel-level | 3.x (compile flag) | Wrong layer (not LTTng UST) |

### 2.2 Recommended Approach: C Extension + sys.monitoring

**Primary**: `sys.monitoring` (Python 3.12+) — low overhead, per-event control, per-code-object filtering  
**Fallback**: `sys.setprofile` (Python 3.8+) — broader compatibility, slightly higher overhead

The implementation follows PInsight's established callback→tracepoint pattern:

| Domain | Callback mechanism | Tracepoint layer |
|--------|-------------------|------------------|
| OpenMP | OMPT callbacks | LTTng UST |
| MPI | PMPI wrappers | LTTng UST |
| CUDA | CUPTI subscribers | LTTng UST |
| **Python** | **sys.monitoring callbacks** | **LTTng UST** |

---

## 3. Architecture

### 3.1 Component Overview

```
┌──────────────────────────────────────────────────────────────────┐
│  Python Application (numpy, mpi4py, torch, user code)           │
├──────────────────────────────────────────────────────────────────┤
│  sys.monitoring (PEP 669)       │  sys.setprofile (fallback)     │
│  ┌────────────────────────┐     │  ┌─────────────────────────┐  │
│  │ PY_START callback      │     │  │ PyTrace_CALL callback   │  │
│  │ PY_RETURN callback     │     │  │ PyTrace_RETURN callback │  │
│  │ CALL callback (C ext)  │     │  │                         │  │
│  └────────┬───────────────┘     │  └────────┬────────────────┘  │
├───────────┼─────────────────────┼───────────┼────────────────────┤
│  _pinsight_python C extension   │                                │
│  ┌────────┴───────────────────────────────┴────────────────┐    │
│  │ Extract: qualname, filename, lineno from code object    │    │
│  │ Apply: domain mode check (PINSIGHT_DOMAIN_ACTIVE)       │    │
│  │ Apply: rate control (lexgion lookup, trace_counter)     │    │
│  │ Emit: tracepoint(python_pinsight_lttng_ust, ...)        │    │
│  └────────────────────────────────────────────────────────┘    │
├──────────────────────────────────────────────────────────────────┤
│  LTTng UST ring buffer → CTF trace files                        │
└──────────────────────────────────────────────────────────────────┘
```

### 3.2 Call Path (sys.monitoring)

```
CPython interpreter detects function call
  → sys.monitoring dispatch (check tool active, find callback for PY_START)
  → PyCFunction call: _pinsight_python.on_py_start(code, offset)
    → PyCodeObject* → extract co_qualname, co_filename, co_firstlineno
    → PINSIGHT_DOMAIN_ACTIVE(Python_domain_index) check
    → lexgion lookup (codeptr = code object pointer, LRU cache)
    → trace_bit check → if tracing:
        tracepoint(python_pinsight_lttng_ust, function_begin,
                   qualname, filename, lineno, code_id)
    → return None (or DISABLE for per-code-object filtering)
```

### 3.3 Call Path (sys.setprofile fallback)

```
CPython PyEval loop detects call/return
  → profile function: _pinsight_python.profile_callback(frame, what, arg)
    → PyFrame_GetCode(frame) → PyCodeObject*
    → same extraction + tracepoint path as above
```

---

## 4. Events, Punits, and Scope Analysis

### 4.1 Events

The Python domain has three events, ordered by priority:

| Event ID | Name | sys.monitoring event | Callback signature | Purpose |
|----------|------|---------------------|-------------------|---------|
| 0 | **function** | `PY_START` + `PY_RETURN` | `(code, offset)` / `(code, offset, retval)` | Core: Python function begin/end timing |
| 1 | **c_call** | `CALL` (when callable is C function) | `(code, offset, callable, arg0)` | **Bridge event**: connects Python call site to native code (OMPT/PMPI/CUPTI) |
| 2 | **import** | Detected via `co_qualname` patterns | N/A (derived from function events) | Future: module import begin/end |

**Why `c_call` is critical for PInsight**: The `CALL` event fires **before** a C extension
function executes. For HPC Python, this is the event that bridges the Python call name to
the subsequent native parallel runtime events:

```
CALL event: callable = numpy.linalg.solve     ← c_call tracepoint: Python knows the name
  └→ ompt_parallel_begin (codeptr=0x7f...)     ← OMPT only sees binary address
       └→ ompt_sync_wait_begin                 ← OMPT sees barrier
```

Without `c_call`, PInsight would show OpenMP parallel regions and MPI calls, but not
**which Python function** triggered them. The `c_call` event is the missing link.

### 4.2 Scopes Higher and Lower Than Function

| Scope | Granularity | Mechanism | Value for PInsight |
|-------|------------|-----------|--------------------|
| **Module import** | Higher than function | Detect `importlib._bootstrap` in `co_qualname` | Shows import overhead (can be 100ms+ in HPC) |
| **Class instantiation** | Same as function | Detect `__init__` in `co_qualname` | Already captured as regular function call |
| **Function** | **Core scope** | `PY_START` / `PY_RETURN` | ✅ Primary tracing unit |
| **C extension call** | Same level, cross-boundary | `CALL` event for C callables | ✅ Bridge to native code |
| **Line** | Lower than function | `sys.monitoring.events.LINE` | ⚠️ Very high overhead; use only for targeted debugging of hot functions |
| **Instruction** | Lowest | `sys.monitoring.events.INSTRUCTION` | ❌ Prohibitive overhead |

**Decision**: Focus on **function** (events 0) and **c_call** (event 1) for initial
implementation. Module import detection (event 2) is derived from function events
(no extra sys.monitoring event needed). LINE is left as a future opt-in for specific
code objects via `set_local_events()`.

### 4.3 Punits

The natural punit for the Python domain is **thread** (`threading.get_ident()`):

| Punit kind | Analogy | Relevance |
|------------|---------|----------|
| **thread** | OpenMP thread | Primary — Python threads are the execution units. Even with GIL, they are distinguishable and schedulable. |
| process | MPI rank | Already handled by PInsight's MPI domain. Python `multiprocessing` creates separate OS processes with separate interpreters. |
| sub-interpreter | (new concept) | Future — PEP 684 (Python 3.12+) gives each sub-interpreter its own GIL. Could become relevant for HPC Python. |

Practical implication: most HPC Python uses the **main thread** for Python code and
delegates parallel work to C extensions (NumPy threads, MPI ranks, CUDA devices).
The default config should trace only `thread 0`:

```ini
[Python.punit.thread]
    range = 0-0    # Main Python thread only
```

For multi-threaded Python (e.g., `concurrent.futures.ThreadPoolExecutor`), the range
can be expanded. For free-threaded Python 3.13+ (PEP 703, no GIL), all threads are
true parallel units and the full range should be used.

---

## 5. Tracepoint Design

### 5.1 LTTng UST Tracepoint Definitions

```c
/* python_lttng_ust_tracepoint.h */
#define LTTNG_UST_TRACEPOINT_PROVIDER python_pinsight_lttng_ust

/* Python function begin — fired on PY_START */
LTTNG_UST_TRACEPOINT_EVENT(python_pinsight_lttng_ust, function_begin,
    TP_ARGS(
        const char *, qualname,
        const char *, filename,
        int, lineno,
        unsigned long, code_id,    /* PyCodeObject pointer as identity */
        int, python_thread_id
    ),
    TP_FIELDS(
        lttng_ust_field_string(qualname, qualname)
        lttng_ust_field_string(filename, filename)
        lttng_ust_field_integer(int, lineno, lineno)
        lttng_ust_field_integer_hex(unsigned long, code_id, code_id)
        lttng_ust_field_integer(int, python_thread_id, python_thread_id)
    )
)

/* Python function end — fired on PY_RETURN */
LTTNG_UST_TRACEPOINT_EVENT(python_pinsight_lttng_ust, function_end,
    TP_ARGS(
        const char *, qualname,
        const char *, filename,
        int, lineno,
        unsigned long, code_id,
        int, python_thread_id
    ),
    TP_FIELDS(
        lttng_ust_field_string(qualname, qualname)
        lttng_ust_field_string(filename, filename)
        lttng_ust_field_integer(int, lineno, lineno)
        lttng_ust_field_integer_hex(unsigned long, code_id, code_id)
        lttng_ust_field_integer(int, python_thread_id, python_thread_id)
    )
)

/* C extension function call — fired on CALL event when callable is C function.
 * This is the bridge event that connects Python-level names to native code
 * traced by OMPT/PMPI/CUPTI. */
LTTNG_UST_TRACEPOINT_EVENT(python_pinsight_lttng_ust, c_call_begin,
    TP_ARGS(
        const char *, caller_qualname,   /* Python function that made the call */
        const char *, callee_name,       /* C function being called (e.g. "numpy.dot") */
        const char *, caller_filename,
        int, caller_lineno,
        unsigned long, code_id
    ),
    TP_FIELDS(
        lttng_ust_field_string(caller_qualname, caller_qualname)
        lttng_ust_field_string(callee_name, callee_name)
        lttng_ust_field_string(caller_filename, caller_filename)
        lttng_ust_field_integer(int, caller_lineno, caller_lineno)
        lttng_ust_field_integer_hex(unsigned long, code_id, code_id)
    )
)
```

### 5.2 Lexgion Identity

For Python, the **lexgion identity** is the `PyCodeObject*` pointer (`code_id` field).
This is analogous to `codeptr_ra` in OMPT — each unique function has a unique code object.

```c
/* In the C extension callback: */
PyCodeObject *code = (PyCodeObject *)args[0];
const void *codeptr = (const void *)code;  /* lexgion identity */
```

The lexgion directory maps `code_id → lexgion_t`, enabling rate-limited tracing,
per-function filtering, and `max_num_traces` auto-trigger — all reusing existing
PInsight infrastructure.

### 5.3 c_call Event: Extracting the C Function Name

The `CALL` event provides `(code, offset, callable, arg0)`. The `callable` argument is
the Python object being called (e.g., `numpy.dot`). To extract the name in C:

```c
/* Extract the qualified name of the C callable */
const char *callee_name = NULL;
if (PyObject_HasAttrString(callable, "__qualname__"))
    callee_name = PyUnicode_AsUTF8(PyObject_GetAttrString(callable, "__qualname__"));
else if (PyObject_HasAttrString(callable, "__name__"))
    callee_name = PyUnicode_AsUTF8(PyObject_GetAttrString(callable, "__name__"));
if (!callee_name) callee_name = "<unknown>";
```

Note: `__qualname__` gives the full dotted path (e.g., `linalg.solve`), while
`__name__` gives just the function name (e.g., `solve`). We prefer `__qualname__`.

---

## 6. Configuration

Python tracing integrates with PInsight's existing INI-style config as a new domain:

```ini
[Python]
    trace_mode = TRACING                # OFF | STANDBY | MONITORING | TRACING
    events = function, c_call           # Events to trace

[Python.punit.thread]
    range = 0-0                         # Main Python thread only

[Lexgion.default]
    max_num_traces = 100                # Rate-limit: trace first 100 calls per function
    trace_mode_after = STANDBY
```

### 6.1 Event Selection Examples

```ini
# Minimal: only Python function begin/end (lowest overhead)
[Python]
    events = function

# Standard: functions + C extension calls (recommended for HPC)
[Python]
    events = function, c_call

# Selective: only C extension calls (bridge events only)
[Python]
    events = c_call
```

### 6.2 Module Filtering (Future Enhancement)

```ini
[Python]
    module_filter = numpy,scipy,mpi4py,torch   # Only trace these modules
    # or
    module_exclude = importlib,_frozen         # Exclude noisy internal modules
```

This would use `sys.monitoring.set_local_events()` to enable/disable tracing per
code object based on `co_filename` module membership.

### 6.3 Domain Registration

Add Python to the domain DSL in `trace_domain_Python.h`:

```c
#define Python_domain_index (num_domain)  /* next available slot */

void pinsight_register_python_domain() {
    DOMAIN_BEGIN("Python");
    DOMAIN_PUNIT("thread");              /* threading.get_ident() */
    DOMAIN_EVENT(0, "function");         /* PY_START + PY_RETURN */
    DOMAIN_EVENT(1, "c_call");           /* CALL for C extension functions */
    DOMAIN_EVENT(2, "import");           /* Future: module import begin/end */
    DOMAIN_END();
}
```

---

## 6. C Extension Module: `_pinsight_python`

### 6.1 Module Structure

```
src/
  python/
    _pinsight_python.c          # C extension: callbacks + LTTng tracepoints
    python_lttng_ust_tracepoint.h  # LTTng TRACEPOINT_EVENT definitions
    pinsight_python.py          # Pure Python setup: register callbacks
    setup.py (or meson.build)   # Build configuration
```

### 6.2 C Extension Implementation Sketch

```c
/* _pinsight_python.c */
#define LTTNG_UST_TRACEPOINT_DEFINE
#include "python_lttng_ust_tracepoint.h"
#include <Python.h>
#include "pinsight.h"
#include "trace_config.h"

static int Python_domain_index = -1;

/* sys.monitoring callback: PY_START event
 * Signature: callback(code: CodeType, instruction_offset: int) */
static PyObject* on_py_start(PyObject *self,
                              PyObject *const *args, Py_ssize_t nargs) {
    if (nargs < 2) Py_RETURN_NONE;

    /* Fast-path domain check */
    if (!PINSIGHT_DOMAIN_ACTIVE(Python_domain_index))
        Py_RETURN_NONE;

    PyCodeObject *code = (PyCodeObject *)args[0];
    const char *qualname = PyUnicode_AsUTF8(code->co_qualname);
    const char *filename = PyUnicode_AsUTF8(code->co_filename);
    int lineno = code->co_firstlineno;
    unsigned long code_id = (unsigned long)code;

    /* Lexgion lookup + rate control (reuses existing PInsight infra) */
    lexgion_t *lgp = lexgion_lookup_or_create(code_id, PYTHON_LEXGION, 0);

    if (lgp->trace_bit) {
        tracepoint(python_pinsight_lttng_ust, function_begin,
                   qualname, filename, lineno, code_id);
        lexgion_post_trace_update(lgp);
    }

    lgp->counter++;
    lgp->num_exes_after_last_trace++;

    Py_RETURN_NONE;
}

/* sys.monitoring callback: PY_RETURN event
 * Signature: callback(code: CodeType, instruction_offset: int, retval: object) */
static PyObject* on_py_return(PyObject *self,
                               PyObject *const *args, Py_ssize_t nargs) {
    if (!PINSIGHT_DOMAIN_ACTIVE(Python_domain_index))
        Py_RETURN_NONE;

    PyCodeObject *code = (PyCodeObject *)args[0];
    const char *qualname = PyUnicode_AsUTF8(code->co_qualname);
    const char *filename = PyUnicode_AsUTF8(code->co_filename);
    int lineno = code->co_firstlineno;
    unsigned long code_id = (unsigned long)code;

    lexgion_t *lgp = lexgion_lookup_or_create(code_id, PYTHON_LEXGION, 0);
    if (lgp->trace_bit) {
        tracepoint(python_pinsight_lttng_ust, function_end,
                   qualname, filename, lineno, code_id);
    }

    Py_RETURN_NONE;
}

/* sys.monitoring callback: CALL event (for C extension functions)
 * Signature: callback(code, offset, callable, arg0) */
static PyObject* on_call(PyObject *self,
                         PyObject *const *args, Py_ssize_t nargs) {
    if (nargs < 3) Py_RETURN_NONE;
    if (!PINSIGHT_DOMAIN_ACTIVE(Python_domain_index))
        Py_RETURN_NONE;

    PyObject *callable = args[2];

    /* Only trace C function calls (not Python→Python calls,
     * which are already covered by PY_START/PY_RETURN) */
    if (PyCFunction_Check(callable) || Py_IS_TYPE(callable, &PyMethodDescr_Type)) {
        PyCodeObject *code = (PyCodeObject *)args[0];
        const char *caller_qualname = PyUnicode_AsUTF8(code->co_qualname);
        const char *caller_filename = PyUnicode_AsUTF8(code->co_filename);
        int caller_lineno = code->co_firstlineno;
        unsigned long code_id = (unsigned long)code;

        /* Extract C callable name */
        const char *callee_name = "<unknown>";
        PyObject *nameobj = PyObject_GetAttrString(callable, "__qualname__");
        if (!nameobj) {
            PyErr_Clear();
            nameobj = PyObject_GetAttrString(callable, "__name__");
        }
        if (nameobj) {
            callee_name = PyUnicode_AsUTF8(nameobj);
            Py_DECREF(nameobj);
        } else {
            PyErr_Clear();
        }

        tracepoint(python_pinsight_lttng_ust, c_call_begin,
                   caller_qualname, callee_name, caller_filename,
                   caller_lineno, code_id);
    }

    Py_RETURN_NONE;
}

/* Module methods */
static PyMethodDef methods[] = {
    {"on_py_start",  (PyCFunction)on_py_start,  METH_FASTCALL, "PY_START callback"},
    {"on_py_return", (PyCFunction)on_py_return, METH_FASTCALL, "PY_RETURN callback"},
    {"on_call",      (PyCFunction)on_call,      METH_FASTCALL, "CALL callback (C ext)"},
    {NULL, NULL, 0, NULL}
};

/* Module definition */
static struct PyModuleDef module = {
    PyModuleDef_HEAD_INIT, "_pinsight_python", NULL, -1, methods
};

PyMODINIT_FUNC PyInit__pinsight_python(void) {
    /* Register Python domain with PInsight */
    pinsight_register_python_domain();
    Python_domain_index = find_domain_index("Python");
    return PyModule_Create(&module);
}
```

### 6.3 Python Setup Script

```python
# pinsight_python.py — Import this to activate PInsight Python tracing
import sys
import _pinsight_python

TOOL_ID = 3  # sys.monitoring tool ID (0-5 available)

def activate(events=("function", "c_call")):
    """Register PInsight callbacks with sys.monitoring.
    
    Events:
      - 'function': PY_START + PY_RETURN (Python function begin/end)
      - 'c_call':   CALL event for C extension functions (bridge to native code)
    """
    sys.monitoring.use_tool_id(TOOL_ID, "pinsight")
    event_mask = 0

    if "function" in events:
        sys.monitoring.register_callback(
            TOOL_ID, sys.monitoring.events.PY_START,
            _pinsight_python.on_py_start)
        sys.monitoring.register_callback(
            TOOL_ID, sys.monitoring.events.PY_RETURN,
            _pinsight_python.on_py_return)
        event_mask |= sys.monitoring.events.PY_START
        event_mask |= sys.monitoring.events.PY_RETURN

    if "c_call" in events:
        sys.monitoring.register_callback(
            TOOL_ID, sys.monitoring.events.CALL,
            _pinsight_python.on_call)
        event_mask |= sys.monitoring.events.CALL

    sys.monitoring.set_events(TOOL_ID, event_mask)
    print(f"PInsight: Python tracing activated (events: {', '.join(events)})")

def deactivate():
    """Deregister PInsight callbacks."""
    sys.monitoring.set_events(TOOL_ID, 0)
    sys.monitoring.free_tool_id(TOOL_ID)
    print("PInsight: Python tracing deactivated")
```

### 6.4 User Usage

```python
# At the top of the Python HPC script:
import pinsight_python
pinsight_python.activate()

# ... normal Python code ...
import numpy as np
result = np.linalg.solve(A, b)   # traces: Python function_begin → OpenMP parallel_begin → ...
```

Or via environment variable (for instrumentation without source modification):
```bash
PYTHONSTARTUP=pinsight_python_activate.py python my_simulation.py
```

---

## 7. Integration with PInsight Control Thread

The Python domain integrates with the existing control thread for:

### 7.1 Mode Transitions
- **TRACING → STANDBY**: `on_py_start` checks `PINSIGHT_DOMAIN_ACTIVE()` and returns
  immediately when mode is STANDBY (near-zero overhead).
- **OFF**: Could use `sys.monitoring.set_events(TOOL_ID, 0)` to disable events at the
  CPython level, achieving true zero overhead. This is analogous to CUPTI's
  `cuptiEnableCallback(0)`.

### 7.2 INTROSPECT
Python tracing participates in the INTROSPECT cycle — when a Python lexgion reaches
`max_num_traces`, it fires the same auto-trigger mechanism. The analysis script receives
Python trace data alongside OpenMP/MPI/CUDA traces.

### 7.3 SIGUSR1 Config Reload
On `SIGUSR1`, the control thread reloads the config file. If the Python domain mode
changes, the C extension calls `sys.monitoring.set_events()` to enable/disable events
at the CPython level.

---

## 8. Overhead Analysis

### 8.1 Per-Function-Call Cost Breakdown

| Component | Cost (estimated) | Notes |
|-----------|:----------------:|-------|
| CPython monitoring dispatch | ~20 ns | Check tool active, find callback |
| PyCFunction call (FASTCALL) | ~15 ns | Vectorcall protocol, no tuple packing |
| Extract qualname/filename | ~10 ns | `PyUnicode_AsUTF8` (cached C string) |
| Domain mode check | ~2 ns | Volatile load + branch |
| Lexgion LRU lookup | ~20 ns | Same as OMPT callback path |
| LTTng tracepoint write | ~50-200 ns | Ring buffer write (when tracing) |
| **Total (tracing active)** | **~120-270 ns** | |
| **Total (STANDBY mode)** | **~35-50 ns** | Domain check → immediate return |

### 8.2 Comparison

| Tool | Mechanism | Per-call overhead |
|------|-----------|:-----------------:|
| PInsight Python (STANDBY) | sys.monitoring + C ext | ~40 ns |
| PInsight Python (TRACING) | sys.monitoring + C ext | ~200 ns |
| cProfile | PyEval_SetProfile (C) | ~300 ns |
| line_profiler | sys.settrace | ~1000 ns |
| py-spy | sampling (no per-call cost) | ~0 ns (statistical) |

---

## 9. Implementation Plan

### Phase 1: Minimal Viable Tracing (1-2 days)

- [ ] Create `src/python/` directory
- [ ] Implement `python_lttng_ust_tracepoint.h` (function_begin, function_end, c_call_begin)
- [ ] Implement `_pinsight_python.c` with `on_py_start`, `on_py_return`, and `on_call`
- [ ] Implement `pinsight_python.py` setup script with event selection
- [ ] Build system: add Python extension to CMakeLists.txt (find_package Python3)
- [ ] Basic test: trace a simple Python+NumPy script, verify events in babeltrace2
- [ ] Verify c_call events correctly bridge Python names to OpenMP parallel regions

### Phase 2: PInsight Integration (1-2 days)

- [ ] Register Python as a new domain in `trace_domain_Python.h`
- [ ] Add `Python` section support to config parser
- [ ] Integrate lexgion lookup/rate-control with existing infrastructure
- [ ] Domain mode check (`PINSIGHT_DOMAIN_ACTIVE`) in callbacks
- [ ] `pinsight_check_pause()` integration for INTROSPECT pause
- [ ] Test: config file with `[Python] trace_mode = TRACING` + rate limiting

### Phase 3: sys.setprofile Fallback (0.5 day)

- [ ] Add `PyEval_SetProfile`-based fallback for Python <3.12
- [ ] Auto-detect Python version and select mechanism
- [ ] Test on Python 3.8, 3.10, 3.12+

### Phase 4: TraceCompass Integration (0.5 day)

- [ ] Add Python state system to TraceCompass XML
- [ ] Python function time graph view (analogous to OpenMP thread view)
- [ ] Cross-domain correlation view (Python → OpenMP/MPI/CUDA)

### Phase 5: Module Filtering and Optimization (1 day)

- [ ] Implement `module_filter` / `module_exclude` config options
- [ ] Use `sys.monitoring.set_local_events()` for per-code-object enable/disable
- [ ] Benchmark overhead on real HPC Python workloads (NumPy, mpi4py)
- [ ] Optimize: cache `PyUnicode_AsUTF8` results per code object in lexgion

### Phase 6: Evaluation (1-2 days)

- [ ] Overhead benchmark: Python+NumPy matrix operations with/without PInsight
- [ ] Cross-domain demo: mpi4py + NumPy traced end-to-end
- [ ] Compare with cProfile and py-spy overhead
- [ ] TraceCompass screenshots for paper

---

## 10. Open Questions

1. **GIL and thread safety**: Since Python has the GIL, only one thread executes Python
   code at a time. But the lexgion directory is per-thread TLS — should Python callbacks
   use the main thread's TLS, or create a Python-specific TLS?

2. **free-threaded Python (3.13+)**: PEP 703 removes the GIL. If PInsight needs to support
   `python3.13t`, the callbacks must be thread-safe. The existing lexgion LRU and volatile
   domain mode checks should be sufficient, but needs verification.

3. **String lifetime**: `PyUnicode_AsUTF8()` returns a pointer into the Python object's
   internal buffer. This is safe as long as the code object is alive (which it is during
   the callback), but LTTng copies the string into the ring buffer immediately, so there's
   no lifetime concern.

4. **Activation method**: Should Python tracing be activated via:
   - (a) `import pinsight_python` in user code (explicit)
   - (b) `PYTHONSTARTUP` environment variable (transparent)
   - (c) Site-packages `.pth` file (always-on, like PInsight's LD_PRELOAD)
   - (d) All of the above?

5. **Minimum Python version**: sys.monitoring requires 3.12+. Should we support older
   Python (3.8+) via sys.setprofile fallback, or target 3.12+ only?
