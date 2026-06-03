---
name: python-support
description: "Python tracing support via sys.monitoring (PEP 669): Phases 1-5 complete and pushed to hip-rocm-support branch"
metadata: 
  node_type: memory
  type: project
  originSessionId: 9426b4f4-624f-4593-9d09-ae7065a7ae13
---

Python is the 4th PInsight tracing domain, added via `sys.monitoring` (PEP 669, Python 3.12+).
Status: **Phases 1-5 complete, tested on Linux with LTTng, pushed to `hip-rocm-support` branch (commit 02f85bc, 2026-06-01)**.

**Why:** Cross-domain correlation — Python → OpenMP / MPI / CUDA on a single LTTng timeline.

**How to apply:** When suggesting improvements, keep the two-library split and O(1) hot-path performance in mind.

## Two-library architecture (key distinction from other domains)

| Library | Load mechanism | Contains |
|---------|---------------|----------|
| `libpinsight.so` | LD_PRELOAD | Domain registration (`register_Python_trace_domain()`) runs in `__attribute__((constructor))` before Python starts. Assigns `Python_domain_index`. |
| `_pinsight_python.cpython-*.so` | `import` in `pinsight.py` | 4 `sys.monitoring` callbacks + LTTng tracepoints + `set/reset_trace_mode`. Links against `libpinsight.so`. |

`pinsight.py` is the Python launcher: `python3 -m pinsight <script.py>`. Uses `TOOL_ID=3` for `sys.monitoring`.

## Events (5, dense IDs 0-4)

| ID | Name | sys.monitoring event | Default |
|:--:|------|---------------------|---------|
| 0 | `pysysmon_py_start`  | `PY_START`  | ON |
| 1 | `pysysmon_py_return` | `PY_RETURN` | ON |
| 2 | `pysysmon_c_start`   | `CALL`      | ON |
| 3 | `pysysmon_c_return`  | `C_RETURN`  | ON |
| 4 | `pysysmon_import`    | reserved    | OFF |

Subdomains: `function` (0,1), `bridge` (2,3), `others` (4).
Punit: `thread` 0-255, via `pysysmon_get_thread_id()` (sequential atomic counter, separate from `global_thread_num`).

## Thread ID namespaces
- `pysysmon_thread_id` (0,1,2…): user-facing, for punit config filtering
- `global_thread_num` (3000,3001…): PInsight-internal, for trace record stitching (avoids collision with OpenMP 0+ / CUDA 2000+)
- Thread 0 = main Python thread; threads 1-N = worker threads (assigned at first callback)

## STANDBY startup (key correctness fix)

`starting_mode = TRACING` but `initial_setup_trace_config()` saves config/env-resolved mode
to `last_mode`, then forces the domain to `STANDBY`. This prevents stdlib import lexgion overflow
(`threading + concurrent.futures` alone create 500+ unique `PyCodeObject*` entries).

- `set_trace_mode()`: atomically restores `last_mode`; called by `activate()` after `set_events()`
- `reset_trace_mode()`: saves current mode to `last_mode`, forces STANDBY; called by `deactivate()`
- `__ATOMIC_SEQ_CST` store ensures all threads see the change immediately

Python 3.14+ fix: `C_RETURN`/`C_RAISE` callbacks must be unregistered before `set_events(TOOL_ID, 0)`.

## Named Lexgion Matching (Phase 3-4, implemented)

Generalized across all domains. Config syntax:
- `[Lexgion(Python:solver.compute)]` — Python by `co_qualname`
- `[Lexgion(Python:solver.py:MyClass.compute)]` — with filename disambiguation
- `[Lexgion(Python).default]` — domain-specific default (priority over global default)
- `[Lexgion(Python:a, Python:b)]: Python.default` — multiple names, inherited settings

**Design:** `lgp->name` / `lgp->filename_hint` store borrowed `co_qualname`/`co_filename`
UTF-8 pointers (safe: code objects live for the app lifetime). `name_resolved_gen` tracks
config-reload generation — re-resolution happens once per SIGUSR1 cycle, not per call.
Forces `trace_config_change_counter = (unsigned int)-1` to trigger config re-resolve.

## Thread/Punit Filtering (Phase 5, implemented)

Config syntax: `[Lexgion(Python:name)] : Python.default : Python.thread(N-M)`

Bug fixes that made this work:
1. `domain_punit_set_match`: was `domain_punit_set->set` (always index 0 = OpenMP), fixed to `domain_punit_set[i].set`
2. `domain_punit_set_set`: parse_punit_set_string populated the bitset but never set the guard flag; added `domain_punit_set_set = 1`

## Test suite

| Script | Coverage |
|--------|----------|
| `test/python_simple/test_phase3.sh` | Rate control, event filtering |
| `test/python_simple/test_phase4.sh` | Named lexgion matching (qualname, filename disambiguation) |
| `test/python_simple/test_phase5.sh` | Thread/punit filtering (5 scenarios, 16 checks) |

## Next steps (as of 2026-06-01)

1. **Thread namespace collision check** — Python uses `global_thread_num` starting at 3000; HIP starts at 4000. Verify no overlap when Python + HIP run together.

2. **Cross-domain correlation** — Python → OpenMP/MPI/CUDA on one timeline. Test Python → HIP on MI300A once HIP support lands.

3. **Heavy imports after set_trace_mode()** — known limitation: `import numpy` inside user script (after tracing window opens) still fills lexgion slots. Mitigation: pre-import or increase `MAX_NUM_LEXGIONS`.
