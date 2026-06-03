# PInsight C/C++ Function Tracing Support: Design

**Date:** 2026-06-03
**Status:** Design (not yet implemented)
**Author:** Y. Yan

---

## 1. Motivation

PInsight already traces OpenMP parallel regions, MPI collectives, CUDA/HIP kernels, and
Python functions. The missing layer is **arbitrary C/C++ function boundaries** — the
compute kernels, solver loops, and library calls that the parallel runtimes operate inside.

Adding C/C++ function tracing enables:

1. **Fine-grained region attribution**: See which C/C++ function triggered an OpenMP
   parallel region or CUDA kernel launch, without source annotation.
2. **Hybrid tracing**: Combine C function begin/end with OpenMP/MPI/HIP events on a
   single LTTng timeline to reconstruct the full execution call graph.
3. **Selective profiling**: Instrument only the compute kernel TUs (not the whole
   program), giving low-overhead tracing of hot paths.
4. **Named lexgion config without source changes**: Users can write
   `[Lexgion(C:my_solver)]` in a config file to rate-limit or auto-trigger INTROSPECT
   on a specific C function without recompiling.

### Target Use Cases

```
C function: solver_iterate()              ← cfunc_pinsight_lttng_ust:func_begin
  └→ OpenMP: parallel region              ← ompt_pinsight_lttng_ust:parallel_begin
       └→ barrier                         ← ompt_pinsight_lttng_ust:sync_wait_begin
C function: exchange_halos()              ← cfunc_pinsight_lttng_ust:func_begin
  └→ MPI: Allreduce                       ← pmpi_pinsight_lttng_ust:MPI_Allreduce_begin
C function: AMReX::FillPatchSingleLevel() ← cfunc_pinsight_lttng_ust:func_begin
  └→ HIP: kernel launch                   ← roctracer_pinsight_lttng_ust:kernel_begin
```

---

## 2. Design Options Analysis

### 2.1 C/C++ Instrumentation Mechanisms

| Approach | Overhead | Granularity | Runtime toggle | Compiler req. | PInsight fit |
|----------|----------|-------------|----------------|---------------|-------------|
| **`-finstrument-functions`** | Low (per-call, compile-time) | Every function in TU | No (compile-time) | GCC + Clang | ⭐ Best |
| Manual source annotation (`PINSIGHT_REGION_BEGIN/END`) | Zero (only where placed) | User-chosen | Yes | None | Good for targeted use |
| LD_PRELOAD / weak symbol interposition | Zero base | Per-symbol only | Yes | None | Only for interceptable symbols |
| Intel PIN / DynamoRIO binary instrumentation | High | Any instruction | Yes | None | Too heavy for HPC philosophy |
| Linux uprobe / eBPF uretprobe | Low | Any ELF symbol | Yes (BPF prog) | None | Wrong layer (kernel-space, not LTTng UST) |
| GCC `-pg` (gprof) | Medium | Every function | No | GCC | gprof-specific, not composable |

### 2.2 Recommended Approach: `-finstrument-functions`

GCC and Clang both support compiler-generated entry/exit hooks via:

```c
void __cyg_profile_func_enter(void *func, void *call_site)
    __attribute__((no_instrument_function));
void __cyg_profile_func_exit(void *func, void *call_site)
    __attribute__((no_instrument_function));
```

- `func` = address of the entering/exiting function → maps to `codeptr_ra`
- `call_site` = address of the call instruction → stored as tracepoint field

This fits PInsight's existing model exactly:

| Domain | Callback mechanism | codeptr_ra source |
|--------|-------------------|-------------------|
| OpenMP | OMPT callbacks | `codeptr_ra` from OMPT event |
| MPI | PMPI wrappers | `&MPI_Allreduce` (function pointer) |
| CUDA | CUPTI subscribers | kernel function pointer |
| Python | `sys.monitoring` | `PyCodeObject*` |
| **C/C++** | **`-finstrument-functions`** | **`void *func` (function start address)** |

### 2.3 Overhead Control

Unlike Python's `sys.monitoring.set_events(TOOL_ID, 0)` which silences callbacks at the
CPython level, `-finstrument-functions` hooks are compiled in permanently. Runtime control
is provided by PInsight's trace_bit and 4-mode hierarchy:
- **OFF/STANDBY**: callback fires but returns after a single volatile load + branch (~5–15 ns)
- **MONITORING/TRACING**: full lexgion lookup + tracepoint emit

The primary overhead control mechanism is therefore **selective TU compilation**: only
compile the compute kernel translation units with `-finstrument-functions`. The main
driver, I/O, and glue code are compiled normally and incur zero overhead.

---

## 3. Architecture

### 3.1 Component Overview

```
┌──────────────────────────────────────────────────────────────────────┐
│  C/C++ Application (compiled with -finstrument-functions on hot TUs) │
│                                                                      │
│  solver.cpp, kernel.cpp, ...  ← compiled with -finstrument-functions │
│  main.cpp, io.cpp, ...        ← compiled normally (no overhead)      │
├──────────────────────────────────────────────────────────────────────┤
│  GCC/Clang generated hooks (in instrumented TUs only)                │
│  ┌─────────────────────────────────────────────────────────────┐    │
│  │ __cyg_profile_func_enter(void *func, void *call_site)       │    │
│  │ __cyg_profile_func_exit (void *func, void *call_site)       │    │
│  └─────────────────────┬───────────────────────────────────────┘    │
├─────────────────────────┼────────────────────────────────────────────┤
│  libpinsight.so (via LD_PRELOAD)                                     │
│  ┌─────────────────────┴──────────────────────────────────┐        │
│  │ cfunc_callback.c                                        │        │
│  │   domain mode check (PINSIGHT_DOMAIN_ACTIVE)            │        │
│  │   lexgion lookup (codeptr = func addr, LRU cache)       │        │
│  │   lazy name resolution: dladdr(func) → dli_sname        │        │
│  │   C++ demangling: abi::__cxa_demangle()                 │        │
│  │   rate control (lexgion trace_bit)                      │        │
│  │   emit: cfunc_pinsight_lttng_ust:func_begin/end         │        │
│  └────────────────────────────────────────────────────────┘        │
│  ┌──────────────────────────────────────────────────────────┐       │
│  │ pinsight.c     — lexgion LRU, rate control               │       │
│  │ trace_config.c — register_C_trace_domain()               │       │
│  │ control_thread — mode transitions, SIGUSR1               │       │
│  └──────────────────────────────────────────────────────────┘       │
├──────────────────────────────────────────────────────────────────────┤
│  LTTng UST (cfunc_pinsight_lttng_ust:*)                              │
└──────────────────────────────────────────────────────────────────────┘
```

### 3.2 Call Path

```
CPU executes call instruction into instrumented function
  → GCC/Clang-generated prologue calls __cyg_profile_func_enter(func, call_site)
    → PINSIGHT_DOMAIN_ACTIVE(C_domain_index) check  (volatile load, ~2 ns)
      if STANDBY/OFF: return immediately
    → lexgion_begin(C_LEXGION, func)
      → LRU lookup by func address → lexgion_t *lgp
      → if lgp->name == NULL and trace_bit:
          dladdr(func, &info) → info.dli_sname
          abi::__cxa_demangle(info.dli_sname) → lgp->name  (once per function)
    → if lgp->trace_bit:
        lttng_ust_tracepoint(cfunc_pinsight_lttng_ust, func_begin,
                             func_addr, func_name, callsite_addr,
                             global_thread_num, record_id)

function body executes (including any OpenMP/MPI/HIP calls)

  → GCC/Clang-generated epilogue calls __cyg_profile_func_exit(func, call_site)
    → lexgion_end() + if trace_bit:
        lttng_ust_tracepoint(cfunc_pinsight_lttng_ust, func_end, ...)
```

### 3.3 Single Library (unlike Python)

C/C++ function tracing is implemented entirely inside `libpinsight.so` — no second shared
library is needed. `__cyg_profile_func_enter/exit` are linked into `libpinsight.so` and
the application links against it (or it is loaded via `LD_PRELOAD`). This is the same
pattern as OpenMP (OMPT callbacks in `libpinsight.so`) and MPI (PMPI wrappers in
`libpinsight.so`).

---

## 4. Lexgion Identity and Name Resolution

### 4.1 `codeptr_ra` = `void *func`

The `func` argument to `__cyg_profile_func_enter` is the start address of the function.
It is:
- Unique per function definition in the process address space
- Stable for the process lifetime (no JIT recompilation)
- Directly usable as `codeptr_ra` in the existing lexgion LRU

This maps identically to how OMPT provides `codeptr_ra` and how Python uses `PyCodeObject*`.

### 4.2 Lazy Name Resolution via `dladdr()`

Symbol names are resolved **only when `trace_bit` is set** — the same lazy pattern as
Python's `co_qualname` extraction. At the first traced call:

```c
#include <dlfcn.h>
#include <cxxabi.h>

static const char *resolve_func_name(void *func) {
    Dl_info info;
    if (dladdr(func, &info) == 0 || info.dli_sname == NULL)
        return "<unknown>";

    /* C++ demangling */
    int status;
    char *demangled = abi::__cxa_demangle(info.dli_sname, NULL, NULL, &status);
    if (status == 0 && demangled != NULL) {
        /* Store in a persistent buffer — demangled string must outlive the call */
        /* Use a per-lexgion static buffer or strdup into a long-lived pool */
        return demangled;  /* caller must free or pool this */
    }
    return info.dli_sname;  /* fallback: mangled name (C functions are not mangled) */
}
```

`dladdr()` requires the binary to have symbols — use `-g` or at minimum avoid `-s`
(strip). On HPC systems with debug builds or DWARF info, this works reliably.

`lgp->name` stores the resolved string (borrowed pointer, same as Python):
- For C functions: `info.dli_sname` points into the binary's symbol table (stable for
  process lifetime — safe to borrow)
- For C++ functions: the demangled string must be copied or pooled, since
  `abi::__cxa_demangle` allocates with `malloc`

### 4.3 Lexgion Identity: `func` vs `call_site`

| Identity choice | Meaning | Rate control scope |
|---|---|---|
| `func` (chosen) | Function definition | Same rate control for all call sites of a function |
| `call_site` | Specific call instruction | Per-call-site rate control (more granular, more lexgion entries) |

We use `func` for consistency with all other PInsight domains (OMPT, PMPI, CUPTI all use
the function/kernel address). `call_site` is stored as a tracepoint field for analysis
tools to use for call-graph reconstruction.

---

## 5. Events, Punits, and Domain Definition

### 5.1 Events

| Event ID | Name | Hook | Default |
|----------|------|------|---------|
| 0 | `func_begin` | `__cyg_profile_func_enter` | ON |
| 1 | `func_end` | `__cyg_profile_func_exit` | ON |

Subdomains: `function` (events 0,1). Extend later if needed (e.g., a `constructor`
subdomain for C++ object construction).

### 5.2 Punits

The punit is **thread**, same as Python. For C/C++ code:
- If `PINSIGHT_OPENMP` is also enabled: threads already have `global_thread_num` assigned
  by OMPT's `thread_begin` callback (starting at 0). The C function callbacks run on these
  same threads and read `global_thread_num` from TLS directly.
- If `PINSIGHT_OPENMP` is not enabled (pure C/C++ app): a dedicated TLS counter starting
  at 5000+ is used to avoid collision with OpenMP (0+), CUDA (2000+), Python (3000+),
  HIP (4000+).

Config: `[C.punit.thread] range = 0-3` selects which threads to trace.

### 5.3 Domain DSL (`trace_domain_C.h`)

```c
DOMAIN_BEGIN("C")
    DOMAIN_SUBDOMAIN("function")
        DOMAIN_EVENT(0, "func_begin")
        DOMAIN_EVENT(1, "func_end")
    DOMAIN_SUBDOMAIN_END()
    DOMAIN_PUNIT("thread", 0, 255)
DOMAIN_END()
```

### 5.4 Thread ID Namespace

| Domain | `global_thread_num` range |
|--------|--------------------------|
| OpenMP | 0+ |
| CUDA | 2000+ |
| Python | 3000+ |
| HIP | 4000+ |
| C/C++ (no OpenMP) | 5000+ |

When `PINSIGHT_OPENMP` is enabled, C function callbacks on OpenMP threads share the
OpenMP `global_thread_num` — no separate namespace needed.

---

## 6. Tracepoint Design

New file: `src/cfunc_lttng_ust_tracepoint.h`
LTTng provider: `cfunc_pinsight_lttng_ust`

```c
LTTNG_UST_TRACEPOINT_EVENT(
    cfunc_pinsight_lttng_ust, func_begin,
    LTTNG_UST_TP_ARGS(
        unsigned long, func_addr,      /* __cyg_profile_func_enter func arg */
        const char *,  func_name,      /* demangled symbol, NULL if unresolved */
        unsigned long, callsite_addr,  /* __cyg_profile_func_enter call_site arg */
        COMMON_LTTNG_UST_TP_ARGS_GLOBAL
    ),
    LTTNG_UST_TP_FIELDS(
        lttng_ust_field_integer_hex(unsigned long, func_addr, func_addr)
        lttng_ust_field_string(func_name, func_name ? func_name : "<unknown>")
        lttng_ust_field_integer_hex(unsigned long, callsite_addr, callsite_addr)
        COMMON_LTTNG_UST_TP_FIELDS_GLOBAL
    )
)
/* func_end: identical fields */
```

`func_addr` is the raw address — analysis tools can use it for cross-reference with
`nm` or DWARF info when `func_name` is not available. `callsite_addr` enables call-graph
reconstruction.

---

## 7. Named Lexgion Matching

C/C++ function tracing uses the same named lexgion infrastructure as Python, OpenMP, MPI,
and CUDA. The `C:` domain prefix identifies the C/C++ domain:

```ini
[Lexgion(C:solver_iterate)]
max_num_traces = 100
trace_mode_after = MONITORING

[Lexgion(C:AMReX::FillPatchSingleLevel)]  # C++ demangled name
max_num_traces = 50
introspect_script = analyze.sh

[Lexgion(C:exchange_halos)] : C.default : C.thread(0-3)  # thread filter
max_num_traces = 200

[Lexgion(C:libsolver.so:newton_step)]    # library disambiguation
max_num_traces = 10
```

The `lgp->name` field stores the demangled symbol name (resolved lazily on first traced
call). The same generation-counter-based re-resolution on SIGUSR1 config reload applies
as in Python.

### 7.1 Name Disambiguation

When the same function name appears in multiple shared libraries (e.g., `init` in both
`libsolver.so` and `libio.so`), use `dladdr()`'s `info.dli_fname` (shared library path)
for disambiguation:

```ini
[Lexgion(C:libsolver.so:init)]   # matches only init from libsolver.so
[Lexgion(C:libio.so:init)]       # matches only init from libio.so
```

The `lgp->filename_hint` field stores the library basename — same pattern as Python's
`co_filename` disambiguation.

---

## 8. Configuration

```ini
# Environment variable
PINSIGHT_TRACE_C=OFF|STANDBY|MONITORING|TRACING

# Config file
[C]
trace_mode = TRACING

[C.global]
func_begin = on
func_end   = on

[C.punit.thread]
range = 0-3                     # trace only threads 0-3

[Lexgion(C).default]
max_num_traces    = 1000        # C function domain default
trace_mode_after  = MONITORING

[Lexgion(C:solver_iterate)]
max_num_traces   = 100
trace_mode_after = STANDBY
introspect_script = ./analyze.sh
```

This integrates with the existing config parser with a new `[C]` section handler —
same pattern as `[Python]`, `[OpenMP]`, `[MPI]`.

---

## 9. Interaction with OpenMP

When C function tracing is combined with OpenMP (`PINSIGHT_CFUNC + PINSIGHT_OPENMP`),
the instrumented TUs typically contain OpenMP-annotated code. Two cases arise:

**Case 1: The user function calls a parallel region**
```
solver_iterate()            ← cfunc:func_begin
  #pragma omp parallel      ← ompt:parallel_begin  (same thread: thread 0)
    worker_body()           ← cfunc:func_begin  (on each OpenMP thread)
  end parallel              ← ompt:parallel_end
```
This is the target use case — full cross-domain correlation on one LTTng timeline.

**Case 2: OpenMP outlined functions**
GCC/Clang outline `#pragma omp parallel` bodies into functions named `._omp_fn.N` or
`solver_iterate._omp_fn.0`. These will also trigger `__cyg_profile_func_enter`. They
appear as separate lexgions with addresses but potentially no human-readable name from
`dladdr()` (implementation-defined naming). These can be filtered via:
```ini
[C.global]
exclude_pattern = ._omp_fn    # future: pattern-based exclusion
```
Or simply accepted — they provide useful information about the parallel region body
execution on each worker thread.

---

## 10. Overhead Analysis

### 10.1 Per-Call Cost Breakdown

| Component | Cost | Notes |
|-----------|:----:|-------|
| `__cyg_profile_func_enter` call overhead | ~5 ns | GCC-generated prologue, function call |
| Domain mode check (STANDBY) | ~2 ns | Volatile load + branch → return |
| Lexgion LRU lookup | ~20 ns | Same as OMPT path |
| `dladdr()` name resolution | ~300 ns | Once per function, lazy on first trace |
| `abi::__cxa_demangle()` | ~500 ns | Once per function |
| LTTng tracepoint write | ~50–200 ns | Ring buffer write (when TRACING) |
| **Total (STANDBY mode)** | **~7–12 ns** | Domain check + return |
| **Total (TRACING, name cached)** | **~80–230 ns** | Lexgion lookup + tracepoint |
| **Total (TRACING, first call)** | **~880 ns** | Includes one-time name resolution |

### 10.2 Comparison with Python

| Feature | Python (`sys.monitoring`) | C/C++ (`-finstrument-functions`) |
|---------|--------------------------|----------------------------------|
| STANDBY overhead | ~40 ns | ~7–12 ns |
| TRACING overhead | ~200 ns | ~80–230 ns |
| Runtime toggle | Yes (`set_events(TOOL_ID, 0)`) | No (compile-time, mode flag only) |
| Overhead control | Runtime event enable/disable | Compile-time TU selection |
| Symbol names | `co_qualname` (always available) | `dladdr()` (requires symbols) |

### 10.3 Impact of Selective TU Compilation

On a typical HPC application where only the 3–5 compute kernel TUs are compiled with
`-finstrument-functions`:
- **Hot functions** (compute kernels): ~7–12 ns overhead per call in STANDBY
- **All other functions** (I/O, init, glue): zero overhead (not instrumented)
- A function called 10⁸ times/second in STANDBY adds ~1% overhead — acceptable

---

## 11. Build System

New CMake option:

```cmake
option(PINSIGHT_CFUNC "C/C++ function tracing via -finstrument-functions" FALSE)
```

When `PINSIGHT_CFUNC=TRUE`:
- Add `src/cfunc_callback.c` to `SOURCE_FILES`
- Add `src/cfunc_lttng_ust_tracepoint.h`
- Link `-ldl` (for `dladdr`)
- Define `PINSIGHT_CFUNC=1` for conditional compilation

`-finstrument-functions` is **not** added to the `libpinsight.so` build — only to the
user's application TUs. `cfunc_callback.c` and all PInsight internal functions must be
marked `__attribute__((no_instrument_function))` to prevent infinite recursion.

### User Compilation

```makefile
# Compile compute kernels with C function instrumentation
solver.o: solver.cpp
    $(CXX) -finstrument-functions -c solver.cpp -o solver.o

kernel.o: kernel.cpp
    $(CXX) -finstrument-functions -c kernel.cpp -o kernel.o

# Main, I/O, and utility code: no instrumentation
main.o: main.cpp
    $(CXX) -c main.cpp -o main.o

# Link — no special flags needed beyond existing pinsight link
app: main.o solver.o kernel.o
    $(CXX) -o app $^ -L$(PINSIGHT_PREFIX)/lib -lpinsight \
           -Wl,-rpath,$(PINSIGHT_PREFIX)/lib

# Run
LD_PRELOAD=$(PINSIGHT_PREFIX)/lib/libpinsight.so \
PINSIGHT_TRACE_C=TRACING \
PINSIGHT_TRACE_CONFIG_FILE=my_app.config \
lttng-record-trace -- ./app
```

---

## 12. Implementation Plan

### Phase 1: Basic Callback Infrastructure

- [ ] `src/cfunc_lttng_ust_tracepoint.h` — `func_begin` and `func_end` tracepoints
- [ ] `src/cfunc_callback.c` — `__cyg_profile_func_enter/exit` with domain mode check,
      lexgion LRU lookup, and tracepoint emission
- [ ] `src/trace_domain_C.h` — DSL domain definition (2 events, 1 subdomain, 1 punit)
- [ ] `src/trace_config.c` — `register_C_trace_domain()` in `constructor(101)`
- [ ] `CMakeLists.txt` — `PINSIGHT_CFUNC` option, link `-ldl`
- [ ] Initial tracepoint carries `func_addr` (raw hex) and `callsite_addr` only — no
      symbol resolution yet
- [ ] Test: compile `test/hello/helloomp_loop.c` with `-finstrument-functions`, verify
      `func_begin`/`func_end` events appear in `lttng view`

### Phase 2: Symbol Name Resolution

- [ ] `dladdr()` for symbol name lookup in `cfunc_callback.c`
- [ ] `abi::__cxa_demangle()` for C++ name demangling
- [ ] Lazy resolution: only when `lgp->trace_bit` is set (first traced call)
- [ ] `lgp->name` storage (demangled string pool or per-lexgion allocation)
- [ ] `lgp->filename_hint` = `info.dli_fname` basename for library disambiguation
- [ ] `func_name` field populated in tracepoint
- [ ] Test: verify function names appear correctly in trace for C and C++ functions

### Phase 3: Config and Rate Control Integration

- [ ] `[C]` config section handler in `trace_config_parse.c`
- [ ] `PINSIGHT_TRACE_C` environment variable
- [ ] Named lexgion matching: `[Lexgion(C:func_name)]` and
      `[Lexgion(C:libname.so:func_name)]`
- [ ] SIGUSR1 config reload with `name_resolved_gen` generation counter
- [ ] INTROSPECT integration (`pinsight_check_pause()` at `func_begin`)
- [ ] Thread/punit filtering: `[Lexgion(C:name)] : C.default : C.thread(N-M)`
- [ ] Test: config-driven rate control and named lexgion matching

### Phase 4: OpenMP + Cross-Domain Correlation

- [ ] Test combined `PINSIGHT_CFUNC + PINSIGHT_OPENMP` on a real HPC mini-app
- [ ] Verify C function begin/end brackets OpenMP parallel_begin/end on same timeline
- [ ] Handle OpenMP outlined function names (`._omp_fn.N`) — filter or pass through
- [ ] TraceCompass: C function time graph correlated with OpenMP parallel regions
- [ ] Overhead benchmark: measure STANDBY and TRACING overhead on compute-intensive loop

### Phase 5: Advanced Features (Deferred)

- [ ] Pattern-based exclusion (`exclude_pattern` config key) for filtering noisy
      functions (OpenMP outlined regions, C++ constructors, etc.)
- [ ] Per-TU enable/disable without recompilation (via weak symbol override)
- [ ] `call_site` as secondary lexgion key option (per-callsite rate control)
- [ ] Evaluate `-finstrument-functions-exclude-function-list` GCC flag for
      compile-time filtering

---

## 13. Open Questions

1. **Demangled string lifetime and memory management.** `abi::__cxa_demangle()` returns a
   `malloc`-allocated string. Options: (a) `strdup` into a per-lexgion field and `free`
   on domain teardown; (b) a global string intern pool; (c) borrow `info.dli_sname`
   (mangled) for C functions and only demangle C++ names. Decision deferred to Phase 2.

2. **`dladdr()` on stripped binaries.** If the binary is stripped (`-s`), `dli_sname`
   will be NULL. The tracepoint falls back to `func_addr` only. Users must be told to
   build with symbols or use a `.debug` split-debug file.

3. **Thread ID without OpenMP.** For pure C/C++ apps without OpenMP, a TLS counter at
   5000+ is needed. If the app later loads OpenMP dynamically (rare), the namespaces could
   collide. Mitigation: check for `omp_get_thread_num()` availability at first callback.

4. **Interaction with C++ exceptions.** `__cyg_profile_func_exit` may not fire if an
   exception unwinds through the function. The lexgion stack would be left in an
   inconsistent state. Mitigation: detect mismatched begin/end in `lexgion_end()` and
   drain the stack (the same mechanism already used for OpenMP mode-switch stack cleanup).

5. **Inlined functions.** Functions inlined by the compiler have no stack frame and
   receive no instrumentation hook. This is expected and consistent with how OMPT handles
   inlined code. Document as a known limitation.

6. **Shared library instrumented at load time.** If a shared library (e.g., `libsolver.so`)
   is compiled with `-finstrument-functions`, its functions will trigger PInsight callbacks
   whenever that library is loaded — including during `dlopen`. PInsight must be
   initialized before the library is loaded. `LD_PRELOAD` ordering handles this naturally.
