# PInsight Control Thread: Design & Implementation

## Table of Contents

1. [Motivation](#motivation)
2. [Architecture Overview](#architecture-overview)
3. [Design: Single-Writer / Multiple-Reader (SWMR)](#design-single-writer--multiple-reader-swmr)
4. [Implementation Details](#implementation-details)
   - [Control Thread Lifecycle](#control-thread-lifecycle)
   - [Wakeup Mechanism](#wakeup-mechanism)
   - [Signal Handler](#signal-handler)
   - [Control Thread Loop](#control-thread-loop)
5. [Domain-Specific Handling](#domain-specific-handling)
   - [CUDA (CUPTI)](#cuda-cupti)
   - [OpenMP (OMPT)](#openmp-ompt)
   - [MPI (PMPI)](#mpi-pmpi)
6. [Introspection & Application Pause](#introspection--application-pause)
7. [Data Race Analysis](#data-race-analysis)
8. [Signal Safety Analysis](#signal-safety-analysis)
9. [Testing & Validation](#testing--validation)
10. [Known Limitations & Future Work](#known-limitations--future-work)
11. [Files Modified](#files-modified)

---

## Motivation

### Problems with the Previous Architecture

PInsight's prior runtime reconfiguration relied on a **deferred-reconfig pattern** where the
SIGUSR1 signal handler set a flag (`config_reload_requested`), and each domain callback
checked that flag on every invocation:

```
SIGUSR1 → handler sets config_reload_requested = 1
         → next parallel_begin reads flag
         → reloads config + re-registers callbacks
```

This approach had several problems:

1. **Hot-path overhead**: Every `parallel_begin`, every CUPTI callback, and every PMPI wrapper
   checked `config_reload_requested` and `mode_change_requested` on every invocation. These
   checks involved atomic operations (`__atomic_exchange_n`) and conditional branches — all
   wasted cycles in the common case where no reconfiguration is pending.

2. **Code duplication**: Each domain (OMPT, CUPTI, MPI) independently implemented its own
   reconfig handler, totaling ~106 lines of nearly identical boilerplate logic scattered
   across three source files.

3. **Signal handler complexity**: The old signal handler for OFF→\* transitions
   (`pinsight_wakeup_from_off_openmp`) directly called OMPT runtime functions from async
   context. While this happened to work on LLVM libomp, it violated POSIX async-signal-safety
   rules and was fragile.

4. **Tight coupling**: Configuration management logic was interleaved with instrumentation
   callbacks, making both harder to understand, test, and modify independently.

5. **Introspection blocking**: The old INTROSPECT implementation used `sigsuspend()` to
   block the **application thread** that triggered the introspection. In a multi-threaded
   OpenMP application, only the triggering thread would be paused — other threads continued
   executing, resulting in inconsistent application state during introspection.

### Goals

- **Zero reconfig overhead** in the callback hot path — no flag checks, no atomics
- **Centralized management** — all config reload, mode switching, and callback
  registration logic in one place
- **Async-signal-safe** signal handler — only trivial operations
- **All-thread pause** for introspection — block all application threads, not just one
- **Simpler code** — cleaner separation of concerns for each domain callback

---

## Architecture Overview

### Before (Deferred Reconfig)

```
┌─── Signal Handler ───────────────────────────────────────┐
│  config_reload_requested = 1;                            │
│  // POSIX-unsafe: pinsight_wakeup_from_off_openmp()      │
└──────────────────────────────────────────────────────────┘
        │ (flag propagation)
        ▼
┌─── parallel_begin (every invocation) ───────────────────┐
│  if (__atomic_exchange_n(&config_reload_requested, 0)) { │
│      pinsight_load_trace_config(NULL);                   │
│      pinsight_register_openmp_callbacks();               │
│  }                                                       │
│  if (__atomic_exchange_n(&mode_change_requested, 0)) {   │
│      pinsight_register_openmp_callbacks();               │
│  }                                                       │
│  // ... ~45 lines of reconfig logic ...                  │
│  // Then actual callback work                            │
└──────────────────────────────────────────────────────────┘
```

Similar blocks existed in `cupti_callback.c` (~55 lines) and `pmpi_mpi.c` (~6 lines).

### After (Control Thread)

```
┌─── Signal Handler (trivial, async-signal-safe) ─────────┐
│  __atomic_or_fetch(&pending_wakeup_reason,               │
│                    PINSIGHT_WAKEUP_CONFIG_RELOAD, ...);   │
│  sem_post(&control_sem);                                 │
└──────────────────────────────────────────────────────────┘
        │ (semaphore, zero-copy wakeup)
        ▼
┌─── Control Thread (dedicated pthread) ──────────────────┐
│  while (!shutdown) {                                     │
│      sem_wait(&control_sem);        // zero CPU when idle│
│      reason = atomic_exchange(&pending_wakeup_reason, 0);│
│      if (CONFIG_RELOAD) load_config();                   │
│      if (INTROSPECT)    pause_app → run_script → resume; │
│      if (any change)    apply_modes();  // CUPTI only    │
│  }                                                       │
└──────────────────────────────────────────────────────────┘

┌─── Callbacks (hot path — minimal overhead) ─────────────┐
│  // pinsight_check_pause();       // volatile read       │
│  if (!PINSIGHT_DOMAIN_ACTIVE(mode))  // volatile read    │
│      return;                                             │
│  // ... actual tracing work ...                          │
└──────────────────────────────────────────────────────────┘
```

---

## Design: Single-Writer / Multiple-Reader (SWMR)

The core design pattern is **Single-Writer / Multiple-Reader (SWMR)**:

| Role | Thread(s) | Operations |
|------|-----------|------------|
| **Writer** | Control thread (1 dedicated pthread) | Reads config files, writes `volatile` mode flags, calls CUPTI enable/disable, manages pause state |
| **Readers** | Application threads (N OpenMP workers + main thread) | Read `volatile` mode flags at callback entry, check `pinsight_app_paused` |

### Why not locks?

The reader-side check (in every callback) must be as cheap as possible. A `volatile` read
compiles to a single `mov` instruction on x86 (~1 cycle), whereas even an uncontended
`pthread_mutex_lock` involves ~25ns of fence + syscall overhead. Given that callbacks can
fire millions of times per second, the `volatile` approach is preferred.

### Consistency model

The SWMR pattern provides **eventual consistency** for mode flags:

- When the control thread writes `domain_default_trace_config[d].mode = OFF`, reading
  threads may observe the old value for a brief window (typically nanoseconds on x86
  due to store buffer forwarding).
- This is acceptable because mode changes are inherently "best effort" — whether a thread
  traces one extra event before seeing the OFF flag has no correctness impact.
- The `volatile` qualifier prevents the compiler from caching the value in a register,
  ensuring threads re-read from memory on each callback entry.

---

## Implementation Details

### Source Files

| File | Role |
|------|------|
| `src/pinsight_control_thread.h` | Public API: start/stop/wakeup, wakeup reason flags, `pinsight_check_pause()` inline, domain apply declarations |
| `src/pinsight_control_thread.c` | Control thread implementation: loop, signal handler, introspection logic |

### Control Thread Lifecycle

The control thread is tightly integrated with the PInsight library lifecycle:

```c
// enter_exit.c — library constructor (__attribute__((constructor)))
void enter_pinsight_func() {
    // ... LTTng init ...
    pinsight_control_thread_start();    // start thread + init semaphore
    pinsight_install_signal_handler();  // install SIGUSR1 handler
    // ... CUPTI init ...
}

// enter_exit.c — library destructor (__attribute__((destructor)))
void exit_pinsight_func() {
    // ... LTTng tracepoint ...
    pinsight_control_thread_stop();     // signal shutdown + join
}
```

**Key ordering requirements:**
1. The control thread must be started **before** the signal handler is installed, because
   the handler does `sem_post(&control_sem)` — the semaphore must be initialized.
2. The control thread blocks SIGUSR1 on itself immediately after starting (via
   `pthread_sigmask`), so the signal is delivered to an application thread.
3. The control thread must be stopped **after** all domain callbacks have completed
   (guaranteed by the destructor ordering).

### Wakeup Mechanism

The control thread uses a POSIX semaphore (`sem_t control_sem`) for sleep/wakeup:

```c
// Sleep (control thread)
while (sem_wait(&control_sem) == -1 && errno == EINTR)
    continue;

// Wake (any thread, including signal handler)
void pinsight_control_thread_wakeup(int reason) {
    __atomic_or_fetch(&pending_wakeup_reason, reason, __ATOMIC_SEQ_CST);
    sem_post(&control_sem);  // async-signal-safe
}
```

**Why semaphores instead of condition variables?**
- `sem_post()` is explicitly listed as **async-signal-safe** by POSIX (§2.4.3).
- `pthread_cond_signal()` is **not** async-signal-safe.
- Since we need to wake the control thread from within a signal handler, semaphores
  are the only correct POSIX-portable choice.

### Wakeup Reason Flags

Multiple wakeup sources can occur concurrently. The `pending_wakeup_reason` variable
uses a bitmask to coalesce them:

| Flag | Value | Source | Action |
|------|-------|--------|--------|
| `PINSIGHT_WAKEUP_CONFIG_RELOAD` | `0x01` | SIGUSR1 handler, inotify (future) | Re-read config file |
| `PINSIGHT_WAKEUP_MODE_CHANGE` | `0x02` | `pinsight_fire_mode_triggers()` | Apply mode_after changes |
| `PINSIGHT_WAKEUP_INTROSPECT` | `0x04` | `pinsight_fire_mode_triggers()` | Run introspection + pause |

The reason is consumed atomically with `__atomic_exchange_n(..., 0, __ATOMIC_SEQ_CST)` to
prevent lost updates from concurrent wakeup sources.

### Signal Handler

The new signal handler is deliberately minimal:

```c
static void pinsight_sigusr1_handler(int sig) {
    (void)sig;
    __atomic_or_fetch(&pending_wakeup_reason, PINSIGHT_WAKEUP_CONFIG_RELOAD,
                      __ATOMIC_SEQ_CST);
    sem_post(&control_sem);
}
```

**Async-signal-safe analysis:**
- `__atomic_or_fetch` — generates a single `lock or` instruction on x86; safe.
- `sem_post` — explicitly listed as async-signal-safe by POSIX.
- No `fprintf`, no `malloc`, no `fopen` — all non-trivial work is deferred to the
  control thread.

The handler is installed with `SA_RESTART` to prevent system calls in application threads
from being interrupted with `EINTR`.

### Control Thread Loop

```c
static void *pinsight_control_loop(void *arg) {
    // Block SIGUSR1 on this thread
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGUSR1);
    pthread_sigmask(SIG_BLOCK, &mask, NULL);

    while (!control_shutdown) {
        // Sleep until woken — zero CPU
        while (sem_wait(&control_sem) == -1 && errno == EINTR) continue;
        if (control_shutdown) break;

        // Consume all pending reasons atomically
        int reason = __atomic_exchange_n(&pending_wakeup_reason, 0, ...);

        // 1. Config reload
        if (reason & PINSIGHT_WAKEUP_CONFIG_RELOAD) {
            pinsight_load_trace_config(NULL);
            // Reset mode_change_fired so auto-triggers can fire again
        }

        // 2. Introspection (pause + script + resume)
        if (reason & PINSIGHT_WAKEUP_INTROSPECT) { ... }

        // 3. Apply modes to all domains (CUPTI callbacks, etc.)
        if (reason & (RELOAD | MODE_CHANGE | INTROSPECT)) {
            control_apply_all_modes();
        }
    }
    return NULL;
}
```

**Why block SIGUSR1 on the control thread?**

SIGUSR1 is delivered to an arbitrary thread that has not blocked it. If the control thread
receives SIGUSR1 while sleeping in `sem_wait()`, the `SA_RESTART` flag (set in `sigaction`)
would cause `sem_wait` to be restarted after the handler runs. This is correct but
unnecessary — we prefer the signal to be delivered to an application thread so that
the handler's `sem_post` wakes the control thread externally.

Blocking SIGUSR1 on the control thread gives clean separation: app threads handle signals,
control thread handles work.

---

## Domain-Specific Handling

### CUDA (CUPTI)

**Mechanism**: `cuptiEnableCallback(enable, subscriber, domain, cbid)` is a process-global,
thread-safe CUPTI API. It can be safely called from the control thread.

**Implementation**: `pinsight_control_cuda_apply_mode()` in `cupti_callback.c`:
```c
void pinsight_control_cuda_apply_mode(void) {
    int enable = PINSIGHT_DOMAIN_ACTIVE(
        domain_default_trace_config[CUDA_domain_index].mode);
    cupti_set_all_callbacks(enable);
}

static void cupti_set_all_callbacks(int enable) {
    for (int i = 0; i < CUDA_domain_info->num_events; i++) {
        if (CUDA_trace_config->events & (1UL << i)) {
            CUptiResult r = cuptiEnableCallback(
                enable, cupti_subscriber,
                CUPTI_CB_DOMAIN_RUNTIME_API,
                CUDA_domain_info->event_table[i].trace_event_id);
            // ... error handling ...
        }
    }
}
```

**Hot path change**: The old 55-line deferred reconfig block in the CUPTI callback
(`onCuptiCallbackAPI`) was replaced with a 1-line `pinsight_check_pause()` call.

### OpenMP (OMPT)

**Mechanism**: `ompt_set_callback(event, fn)` modifies internal callback tables in the
OpenMP runtime.

**Limitation discovered**: Calling `ompt_set_callback` from the control thread while
OpenMP parallel regions are actively executing causes **SIGSEGV**. The LLVM libomp runtime
maintains internal callback dispatch tables that worker threads read concurrently.
Modifying these tables from another thread creates a data race that manifests as a crash.

> **Note**: A standalone test (`test/openmp/test_ompt_cross_thread.c`) confirmed that
> `ompt_set_callback` *can* be called from a non-OpenMP pthread when no parallel region
> is active. The crash only occurs when worker threads are concurrently dispatching
> callbacks from the same table being modified.

**Current solution**: OMPT callbacks are **not** re-registered from the control thread.
Instead, the `volatile` domain mode flag serves as a killswitch:

```c
// ompt_callback.c — parallel_begin
void on_ompt_callback_parallel_begin(...) {
    if (!PINSIGHT_DOMAIN_ACTIVE(
            domain_default_trace_config[OpenMP_domain_index].mode))
        return;  // volatile read — zero overhead when OFF
    // ... actual tracing work ...
}
```

When the mode is set to OFF, `parallel_begin` returns immediately. Since `parallel_begin`
is the gateway callback — all other OpenMP callbacks (implicit_task, work, sync, etc.)
flow from it — returning early from `parallel_begin` effectively disables all OpenMP
tracing without deregistering callbacks.

**Overhead**: When mode is OFF, each OpenMP event still dispatches through the OMPT
framework to our callback function, which then returns immediately. This costs ~10-20ns
per event (function call overhead). For true zero-overhead deregistration, OMPT callbacks
would need to be re-registered at a safe point — specifically at the next `parallel_begin`
before the team is created (a sequential pre-fork point). This is a future optimization.

**Hot path change**: The old 45-line deferred reconfig block in `parallel_begin` was
removed entirely.

### MPI (PMPI)

**Mechanism**: PMPI wrappers are resolved at link time and cannot be deregistered. The
wrapper functions always execute; the mode check is an early-return guard.

**Implementation**: No apply function needed. The PMPI prologue macro reads the volatile
mode flag directly:

```c
#define PMPI_CALL_PROLOGUE(mpi_call_name) \
    if (!PINSIGHT_DOMAIN_ACTIVE(domain_default_trace_config[MPI_domain_index].mode)) \
        return PMPI_##mpi_call_name(...);
```

**Hot path change**: The old `config_reload_requested` and `mode_change_requested` checks
(~6 lines with atomic operations) were removed. The prologue now has a single `volatile`
read — equivalent cost to the old `PINSIGHT_DOMAIN_ACTIVE` check that already existed.

---

## Introspection & Application Pause

### Problem

The old introspection implementation used `sigsuspend()` in the application thread that
triggered the mode switch. This only paused **one** thread — other OpenMP worker threads
continued executing, leading to inconsistent application state.

### Solution: Control-Thread-Managed Pause

The control thread manages a global pause flag (`volatile int pinsight_app_paused`) and
a condition variable (`pthread_cond_t pinsight_pause_cond`):

```
Control Thread                    App Threads
─────────────                    ───────────
1. Set pinsight_app_paused = 1   ────→ 2. Each callback entry:
                                        pinsight_check_pause()
                                        if (pinsight_app_paused)
                                            pthread_cond_wait(...)
                                            ← blocks ──────────┐
3. Run script, sleep/wait                                       │
4. Set pinsight_app_paused = 0                                  │
5. pthread_cond_broadcast()       ← wakes all ──────────────────┘
```

### Pause Semantics

| timeout value | Behavior |
|---------------|----------|
| `> 0` | Pause for N seconds. Interruptible by SIGUSR1 (`sem_timedwait`). |
| `= 0` | No pause — just run the script and continue. |
| `-1` | Pause indefinitely until SIGUSR1 (`sem_wait`). |

### `pinsight_check_pause()` — The Fast Path

```c
static inline void pinsight_check_pause(void) {
    if (__builtin_expect(pinsight_app_paused, 0)) {
        pthread_mutex_lock(&pinsight_pause_mutex);
        while (pinsight_app_paused) {
            pthread_cond_wait(&pinsight_pause_cond, &pinsight_pause_mutex);
        }
        pthread_mutex_unlock(&pinsight_pause_mutex);
    }
}
```

**Cost**: When not paused (the normal case), this is a single `volatile` read with a
branch prediction hint (`__builtin_expect(..., 0)`). On x86, this compiles to:

```asm
mov    eax, [pinsight_app_paused]
test   eax, eax
jnz    .slow_path          ; predicted not-taken
```

Total cost: ~1ns. The slow path (mutex + condvar) is only taken during introspection
pauses, which are rare and intentional.

---

## Data Race Analysis

For an in-depth analysis of global config object races, see
[`data_race_analysis.md`](data_race_analysis.md). The control thread changes the
following race profiles:

### Improved by Control Thread

| Race | Before | After |
|------|--------|-------|
| **Config reload vs. callback reads** | config reload runs in `parallel_begin` (could race with nested regions) | Runs in control thread (still races with app threads reading config, but same severity) |
| **`mode_change_requested` flag** | Checked + consumed by atomic exchange in `parallel_begin` (every invocation) | **Eliminated** — auto-trigger calls `pinsight_control_thread_wakeup()` instead |
| **`config_reload_requested` flag** | Checked + consumed by atomic exchange in `parallel_begin` (every invocation) | **Eliminated** — signal handler does `sem_post` directly |
| **Signal handler complexity** | Called `pinsight_wakeup_from_off_openmp()` — non-trivially unsafe | **Trivial** — only `sem_post` and `__atomic_or_fetch` |

### Unchanged

| Race | Severity | Notes |
|------|----------|-------|
| **Config reload vs. app reads** | MEDIUM | `pinsight_load_trace_config` modifies `lexgion_*_trace_config` objects that app threads may read concurrently. This is inherent to the non-locking design and acceptable given reloads are rare. Future mitigation: double-buffer with atomic pointer swap. |
| **Concurrent `fire_mode_triggers`** | LOW | Multiple threads can trigger mode changes simultaneously. Benign because they write the same values. |
| **Mode write vs. mode read** | BENIGN | Eventual consistency by design — `volatile` ensures visibility. |

---

## Signal Safety Analysis

### SIGUSR1 Delivery Path

```
User: kill -USR1 <pid>
  │
  ▼
Linux kernel selects a thread that has not blocked SIGUSR1
  │ (control thread blocks it → always delivered to an app thread)
  ▼
pinsight_sigusr1_handler() runs in app thread context
  │
  ├── __atomic_or_fetch(&pending_wakeup_reason, ...)  → lock or [mem], imm
  └── sem_post(&control_sem)                          → futex(FUTEX_WAKE)
  │
  ▼
App thread returns from handler, resumes normal execution (SA_RESTART)
  │
  ▼
Control thread wakes from sem_wait()
  │
  └── Performs config reload, mode changes, etc.
```

### Async-Signal-Safe Functions Used

| Function | POSIX Safe? | Notes |
|----------|-------------|-------|
| `sem_post()` | ✅ Yes | Explicitly listed in POSIX §2.4.3 |
| `__atomic_or_fetch()` | ✅ Yes | Compiles to `lock or` — no library call |
| Assignment to `volatile` | ✅ Yes | Simple store instruction |

### Functions NOT Called from Signal Handler

| Function | Called By | Notes |
|----------|-----------|-------|
| `pinsight_load_trace_config()` | Control thread only | Uses `fprintf`, `fopen`, `malloc` — not signal-safe |
| `pinsight_register_openmp_callbacks()` | Control thread only | Calls `ompt_set_callback` — not signal-safe |
| `cuptiEnableCallback()` | Control thread only | CUDA driver call — not signal-safe |

---

## Testing & Validation

### Test Results

| Test | Method | Result |
|------|--------|--------|
| **Smoke test** | `LD_PRELOAD=... ./vecadd_pinsight 5` | ✅ PASSED |
| **LTTng trace** | `scripts/trace.sh` + `verify_trace.sh` | ✅ 17/17 checks |
| **SIGUSR1 reload** | Background vecadd (50k iter) + `kill -USR1` | ✅ PASSED — "Control thread reloading config" logged, app continues |
| **CUDA mode apply** | `control_apply_all_modes()` with CUPTI enable/disable | ✅ PASSED |
| **No-PInsight baseline** | `./vecadd_pinsight 5` without LD_PRELOAD | ✅ PASSED |

### OMPT Cross-Thread Test

A standalone test (`test/openmp/test_ompt_cross_thread.c`) was created to verify whether
`ompt_set_callback` can be called from a non-OpenMP pthread:

- **Without active parallel region**: ✅ Works — callback registration succeeds.
- **With active parallel region**: ❌ SIGSEGV — concurrent modification of OMPT dispatch
  table while worker threads are reading it.

This test informed the design decision to use volatile mode flags as a killswitch for
OpenMP rather than cross-thread callback re-registration.

---

## Known Limitations & Future Work

### 1. OMPT Callback Deregistration from Control Thread

**Issue**: `ompt_set_callback(event, NULL)` from the control thread causes SIGSEGV when
OpenMP parallel regions are active.

**Current workaround**: Volatile mode flag killswitch — callbacks check
`PINSIGHT_DOMAIN_ACTIVE(mode)` and return immediately when OFF.

**Overhead**: ~10-20ns per OpenMP event (function call into the callback, then immediate
return). Acceptable for most applications; eliminates the need for OMPT-internal
synchronization.

**Future options**:
- **Safe re-registration at `parallel_begin`**: The `parallel_begin` callback runs at a
  sequential pre-fork point before the team is created. At this point, no worker threads
  are reading the dispatch table, so `ompt_set_callback` is safe. A guard in
  `parallel_begin` could check a "pending OMPT changes" flag and re-register callbacks.
- **OpenMP runtime patch**: Request LLVM libomp to make `ompt_set_callback` thread-safe
  (e.g., using an RCU-like pattern for the dispatch table).

### 2. Config Reload Data Race

**Issue**: `pinsight_load_trace_config()` modifies `lexgion_*_trace_config` objects that
app threads may read concurrently (same as the previous architecture).

**Impact**: Brief period of mixed config values during reload. No crash risk since objects
are never freed, only mutated in-place.

**Future mitigation**: Double-buffer the config state — populate a shadow copy, then
atomically swap the pointer. This eliminates the race entirely.

### 3. inotify-Based Config Watching

Currently, config reloads require explicit `SIGUSR1`. A future enhancement would use
Linux `inotify` within the control thread to automatically watch the config file for
changes:

```c
int ifd = inotify_init1(IN_NONBLOCK);
inotify_add_watch(ifd, config_path, IN_MODIFY | IN_CLOSE_WRITE);
```

This would enable config changes to take effect automatically without manual signaling.
The control thread is already structured to support this — it just needs a `poll()` or
`select()` call on the inotify fd alongside the semaphore.

### 4. Per-Thread Pause Granularity

The current pause mechanism blocks **all** threads at their next callback entry point.
A future enhancement could support per-thread or per-domain pausing, though this adds
complexity without a clear use case yet.

---

## Files Modified

### New Files

| File | Lines | Description |
|------|-------|-------------|
| [`src/pinsight_control_thread.h`](../src/pinsight_control_thread.h) | ~113 | Public API: lifecycle, wakeup, pause inline, domain apply declarations |
| [`src/pinsight_control_thread.c`](../src/pinsight_control_thread.c) | ~295 | Control thread loop, signal handler, introspection logic |

### Modified Files

| File | Changes |
|------|---------|
| [`CMakeLists.txt`](../CMakeLists.txt) | Added `pinsight_control_thread.{h,c}` to source list; added `pthread` to linker |
| [`src/trace_config.h`](../src/trace_config.h) | Made `domain_trace_config_t.mode` and `.events` fields `volatile`; removed `config_reload_requested` and `mode_change_requested` extern declarations |
| [`src/trace_config.c`](../src/trace_config.c) | Removed legacy SIGUSR1 handler, atomic flags, and `pinsight_install_signal_handler()` |
| [`src/enter_exit.c`](../src/enter_exit.c) | Added `pinsight_control_thread_start()` + `pinsight_install_signal_handler()` in constructor; added `pinsight_control_thread_stop()` in destructor |
| [`src/pinsight.c`](../src/pinsight.c) | Rewrote `pinsight_fire_mode_triggers()` to use control thread wakeup; removed old introspection code |
| [`src/cupti_callback.c`](../src/cupti_callback.c) | Removed 55-line deferred reconfig block; added `pinsight_check_pause()`; extracted `cupti_set_all_callbacks()` helper and `pinsight_control_cuda_apply_mode()` |
| [`src/ompt_callback.c`](../src/ompt_callback.c) | Removed 45-line deferred reconfig block from `parallel_begin`; removed `pinsight_wakeup_from_off_openmp()`; added `pinsight_control_openmp_apply_mode()` |
| [`src/ompt_callback.h`](../src/ompt_callback.h) | Replaced `pinsight_wakeup_from_off_openmp` declaration with `pinsight_control_openmp_apply_mode` |
| [`src/pmpi_mpi.c`](../src/pmpi_mpi.c) | Removed `config_reload_requested` and `mode_change_requested` checks from `PMPI_CALL_PROLOGUE` macro |

### Lines of Code Impact

| Metric | Before | After | Delta |
|--------|--------|-------|-------|
| Reconfig logic in CUPTI callback | ~55 lines | 0 lines | **−55** |
| Reconfig logic in OMPT parallel_begin | ~45 lines | 0 lines | **−45** |
| Reconfig logic in PMPI prologue | ~6 lines | 0 lines | **−6** |
| Signal handler + flag infrastructure | ~20 lines (complex) | ~4 lines (trivial) | **−16** |
| New: control thread module | 0 | ~408 lines (.h + .c) | **+408** |
| **Net** | | | **+286** (centralized) |

The net increase in total lines is justified by:
- Consolidation of scattered, duplicated logic into a single maintainable module
- Comprehensive introspection pause/resume implementation (all threads, not just one)
- Clear separation of concerns (management vs. instrumentation)
- Foundation for future features (inotify, double-buffered config)
