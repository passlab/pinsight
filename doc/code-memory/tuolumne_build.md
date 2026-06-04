---
name: tuolumne-build
description: "Build setup for PInsight on tuolumne (RHEL 8.10, Cray PE, AMD/El Capitan class node): LTTng from source, GCC for library, clang for traced apps"
metadata: 
  node_type: memory
  type: project
  originSessionId: 9cbf2f1f-c861-41de-9f9a-8c8f9822a95b
---

Tuolumne is an LLNL El Capitan early-access system (RHEL 8.10, AMD EPYC Trento, AMD MI300A GPU, Cray PE).

**Why:** The system LTTng UST (2.8.1) is too old for PInsight's API (needs 2.13+). No NVIDIA GPU — no CUDA. ROCm 7.2.1 available at `/opt/rocm-7.2.1`.

**How to apply:** Use these flags and rules for all PInsight builds on this system.

## LTTng stack — built from source into ~/local

| Package | Version | Location |
|---------|---------|----------|
| userspace-rcu | 0.14.1 | ~/local |
| lttng-ust | 2.13.8 | ~/local |
| lttng-tools | 2.13.14 | ~/local |
| babeltrace2 | 2.0.6 | ~/local |

Environment (in ~/.profile):
```bash
export PATH=$HOME/local/bin:$PATH
export LD_LIBRARY_PATH=$HOME/local/lib:$HOME/local/lib64:...
export PKG_CONFIG_PATH=$HOME/local/lib/pkgconfig:...
```

## PInsight CMake build command (OpenMP + GCC)

```bash
cd build_omp
cmake \
  -DCMAKE_C_COMPILER=gcc \
  -DCMAKE_CXX_COMPILER=g++ \
  -DCMAKE_C_FLAGS="-I$HOME/local/include" \
  -DCMAKE_CXX_FLAGS="-I$HOME/local/include" \
  -DCMAKE_SHARED_LINKER_FLAGS="-L$HOME/local/lib -Wl,-rpath,$HOME/local/lib" \
  -DPINSIGHT_OPENMP=TRUE \
  -DPINSIGHT_MPI=FALSE \
  -DPINSIGHT_CUDA=FALSE \
  -DPINSIGHT_HIP=FALSE \
  -DCMAKE_PREFIX_PATH=$HOME/local \
  ..
```

Three flags are all required:
- `-DCMAKE_C_FLAGS="-I$HOME/local/include"` — use lttng-ust 2.13 headers (system has 2.8.1 at /usr/include/lttng which takes precedence otherwise)
- `-DCMAKE_SHARED_LINKER_FLAGS="-L$HOME/local/lib -Wl,-rpath,..."` — link and load lttng-ust.so.1 from ~/local (system has .so.0)
- `-DCMAKE_PREFIX_PATH=$HOME/local` — for any find_package lookups

## Code fix: ompt_lttng_ust_tracepoint.h

Clang 21 rejects implicit `const void *` → integer conversion in LTTng tracepoint field macros. Applied `(unsigned long int)` / `(long int)` casts to all `lttng_ust_field_integer_hex(...)` calls where the value argument is a `const void *`:
- `parallel_codeptr` in `COMMON_LTTNG_UST_TP_FIELDS_OMPT` (both MPI and non-MPI variants)
- `parent_task_frame` in `parallel_begin`
- `work_begin_codeptr`, `work_end_codeptr` in the WORK macro
- `masked_begin_codeptr`, `masked_end_codeptr` in the MASKED macro
- `sync_codeptr` in both BARRIER macros (replace_all)

GCC 13 accepts the old implicit conversion with a warning (not an error), so this fix is needed for clang builds but also correct practice.

## OMPT application compiler requirement

**libpinsight.so** — build with GCC or clang, no difference.

**Application being traced** — MUST be compiled with `clang -fopenmp` (links `libomp.so`).
Do NOT use `gcc -fopenmp` for the traced app: GCC links `libgomp.so`, causing two OpenMP runtimes in the same process. OMPT callbacks are dispatched by the runtime the app uses; since PInsight targets LLVM libomp's OMPT interface, `libgomp` apps produce 0 trace events.

```bash
# correct — uses libomp, OMPT works
clang -fopenmp -O2 -o myapp myapp.c

# wrong — uses libgomp, 0 OMPT events
gcc -fopenmp -O2 -o myapp myapp.c
```
