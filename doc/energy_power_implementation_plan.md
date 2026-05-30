# Energy/Power Measurement Implementation Plan

**Date:** 2026-05-29
**Author:** Yonghong Yan
**Status:** Planning

---

## Two Features, Two CMake Flags

Energy and power measurement are implemented as two independent, layered features:

| Feature | CMake flag | What it measures | Overhead |
|---------|------------|-----------------|---------|
| **Energy** | `PINSIGHT_ENERGY` | Total application energy (joules) — two counter reads, at library enter and exit | Zero runtime overhead |
| **Power** | `PINSIGHT_POWER` | Power over time (watts) — periodic counter reads by the control thread | Zero on application hot path |

`PINSIGHT_POWER=TRUE` implies `PINSIGHT_ENERGY=TRUE` — periodic polling reuses the same
`energy.c` read functions. CMake enforces this automatically:

```cmake
if(PINSIGHT_POWER AND NOT PINSIGHT_ENERGY)
    set(PINSIGHT_ENERGY TRUE)
endif()
```

The naming maps directly to physics: **Energy** (E) is a scalar total from two counter
readings. **Power** (P = dE/dt) requires a time series.

---

## Before You Implement — Files to Read First

A future implementation session must read these files before writing any code.
The plan references them but does not reproduce their content.

### 1. `src/trace_config_parse.c` — understand the config parser pattern

The `[Energy]` and `[Power]` sections follow the same INI-style parser already used
for `[OpenMP.global]`, `[Lexgion]`, etc. Read `trace_config_parse.c` to understand:
- How section headers (`[SectionName]`) are detected and dispatched
- How key=value pairs are parsed within a section
- How `on`/`off` boolean values are read
- How existing range syntax (`range = 0-3`) is parsed for punits — the same function
  is reused for `intel_cpu_sockets = 0,1` and `nvidia_gpu_devices = 2`
- Where to register the new `[Energy]` and `[Power]` section handlers

### 2. `src/trace_config.h` — understand where the new struct fits

Read `trace_config.h` to see the existing config structures (`lexgion_trace_config_t`,
`domain_trace_config_t`, etc.) and find the right place to add `energy_power_config_t`.
Also check whether `energy_power_config` should be declared `extern` alongside the
existing global config variables.

### 3. `src/ompt_lttng_ust_tracepoint.h` lines 62–82 — the macros to delete

The three macros that must be removed are:
```c
#define ENERGY_LTTNG_UST_TP_ARGS       /* ~line 63 */
#define ENERGY_LTTNG_UST_TP_FIELDS     /* ~line 70 */
#define ENERGY_LTTNG_UST_TRACEPOINT_CALL_ARGS  /* ~line 78 */
```
And their empty `#else` counterparts at ~lines 80–82. Also remove the `package_energy`
global variable declaration referenced in the comment at ~line 76.

### 4. `src/ompt_callback.c` — the four energy blocks to delete

Four `#ifdef PINSIGHT_ENERGY` blocks to remove (search for `PINSIGHT_ENERGY`):
- `thread_begin` (~line 234): `rapl_sysfs_read_packages(package_energy)`
- `thread_end` (~line 324): same call
- `parallel_begin` (~line 451): guarded by `if (global_thread_num == 0)`
- `parallel_end` (~line 488): guarded by `if (global_thread_num == 0)`

Also remove the `package_energy[]` global array declaration wherever it lives in
`ompt_callback.h` or at the top of `ompt_callback.c`.

### 5. `src/enter_exit.c` — exact wiring points

Read `enter_exit.c` to confirm the constructor/destructor structure before adding
energy calls. The empty `#ifdef PINSIGHT_ENERGY` stub at ~line 53 is the placeholder —
replace it with the full init/read/tracepoint sequence shown in section 1.5.

### 6. `src/pinsight_control_thread.c` — the existing loop before extending it

Read the `pinsight_control_loop()` function to understand the `sem_wait` /
`sem_timedwait` structure before adding the power polling branch. Pay attention to:
- The `control_shutdown` flag and how the loop exits
- The existing `sem_timedwait` path used for INTROSPECT (the power polling reuses
  this pattern but as the primary loop mode, not a special case)
- Where `control_apply_all_modes()` is called — the energy sample emission sits
  in the timeout branch, separate from the signal-woken branch

---

## Background and Design Decisions

### What we measure

RAPL (Intel), amd_energy (AMD), and GPU energy interfaces expose **monotonically increasing
hardware accumulators** — not instantaneous power. The design reads these counters at two
points and diffs them:

```
total_energy  = exit_counter  - enter_counter    (µJ per socket / mJ per GPU device)
average_power = total_energy  / (t_exit - t_enter)   (Watts)
```

LTTng timestamps already on every tracepoint provide `t_enter` and `t_exit` for free.

### Why not read inside domain callbacks (OpenMP, HIP, MPI, …)

Hardware counters update at ~1 ms resolution. Short regions (<1 ms) would read identical
values at begin and end — delta = 0, no information, pure overhead. The one-thread-reads-
all-sockets pattern is already correct at enter/exit; spreading reads across OMPT/HIP
callbacks adds complexity with no gain.

### Thread ownership

CPU energy counters are **global hardware state** — any thread can read any socket's
counter. The main thread (which runs `enter_pinsight_func` / `exit_pinsight_func`) is the
natural single reader for `PINSIGHT_ENERGY`. For `PINSIGHT_POWER`, the dedicated PInsight
control thread takes over periodic polling, keeping all energy code off the application's
hot path.

### Energy/Power is NOT a domain

The domain framework (OpenMP, MPI, CUDA, HIP, Python) handles **intercepted runtime
events** — callbacks pushed by the runtime at specific code locations. Its value comes from
lexgions, per-lexgion rate control, punits, and the 4-mode hierarchy. None of these
concepts apply to energy/power:

| Domain concept | Applies to energy/power? | Reason |
|----------------|--------------------------|--------|
| Lexgions (code locations) | No | RAPL counters are global hardware state, not tied to any source location |
| Rate control (max_num_traces, tracing_rate) | No | Sampling is time-driven, not code-driven |
| Named lexgion config | No | No code identity for a hardware counter |
| Punits (thread, rank, stream) | No | Energy is per-socket or per-device, not per execution unit |
| 4-mode hierarchy (OFF/STANDBY/MONITORING/TRACING) | No | STANDBY and MONITORING have no meaningful interpretation for polling |
| Callback-driven events | No | Energy is pull (polled), not push (callback) |

Energy/Power is a **measurement service** with its own CMake flags, its own tracepoint
provider (`energy_pinsight_lttng_ust`), and its own `[Energy]` / `[Power]` config
sections. Runtime on/off control per platform type is sufficient — no 4-mode state machine
is needed.

### Remove the old RAPL implementation

`src/rapl.c` / `src/rapl.h` use `fopen`/`fscanf`/`fclose` per read and are Intel-only.
They are replaced by the new `src/energy.c` / `src/energy.h`. The `ENERGY_LTTNG_UST_TP_*`
macros in `ompt_lttng_ust_tracepoint.h` that piggy-back energy onto OMPT events are also
removed — energy gets its own dedicated tracepoints.

---

## Three-Layer Control

Control is independent at three layers. Crucially, `[Energy]` and `[Power]` have
**independent** socket/device selection — enabling all sockets for energy snapshots does
not force the same set for power polling, and vice versa:

```
Layer 1 — CMake (compile-time)     Layer 2 — [Energy] config              Layer 3 — [Power] config
────────────────────────────────   ────────────────────────────────────    ──────────────────────────────────────
PINSIGHT_ENERGY                →   intel_cpu  = on/off                    intel_cpu_poll_interval_ms = 10
                                   intel_cpu_sockets = 0,1  (enter/exit)  intel_cpu_sockets = 0  (poll only)
PINSIGHT_ENERGY                →   amd_cpu    = on/off
                                   amd_cpu_sockets = 0,1
PINSIGHT_ENERGY                →   arm_cpu    = on/off
                                   arm_cpu_sockets = all
PINSIGHT_ENERGY_NVIDIA         →   nvidia_gpu = on/off                    nvidia_gpu_poll_interval_ms = 100
                                   nvidia_gpu_devices = 0,1,2             nvidia_gpu_devices = 2  (poll only)
PINSIGHT_ENERGY_AMD_GPU        →   amd_gpu    = off  (no snapshot)        amd_gpu_poll_interval_ms = 200
                                                                           amd_gpu_devices = 1  (poll anyway)
PINSIGHT_ENERGY_INTEL_GPU      →   intel_gpu  = on/off
                                   intel_gpu_devices = all
```

A platform with `poll_interval_ms > 0` in `[Power]` **is polled even if disabled in
`[Energy]`** — the two features are fully independent. Runtime config can only activate
what is compiled in; specifying `nvidia_gpu_poll_interval_ms = 100` when
`PINSIGHT_ENERGY_NVIDIA` is not set is silently ignored.

---

## Config Design

`[Energy]` and `[Power]` are fully independent sections. Each carries its own
per-platform enable and socket/device selection. `[Power]` inherits `[Energy]`'s
socket/device selection as its **default** when the key is absent — so you only need
to write what differs from the snapshot selection.

### `[Energy]` section — per-platform enable + socket/device selection for enter/exit reads

```ini
[Energy]
# Per-platform enable (default: on for platforms detected at runtime)
# Per-platform socket/device selection (default: all when key absent)
intel_cpu           = on
intel_cpu_sockets   = 0,1       # sockets to read at enter/exit

amd_cpu             = on
amd_cpu_sockets     = 0,1

arm_cpu             = off       # disabled even if hardware present

nvidia_gpu          = on        # requires PINSIGHT_ENERGY_NVIDIA at compile time
nvidia_gpu_devices  = 0,1,2     # GPU devices to read at enter/exit

amd_gpu             = off       # no enter/exit snapshot for AMD GPU
                                # (can still be polled by [Power] independently)

intel_gpu           = off       # requires PINSIGHT_ENERGY_INTEL_GPU
```

### `[Power]` section — per-platform poll intervals + independent socket/device selection

`0` for an interval disables polling for that platform — the interval is the enable/
disable control, no separate flag needed. Socket/device keys are **optional**: if absent,
the value from `[Energy]` is inherited; if present, it applies to polling only.

```ini
[Power]
# Per-platform poll interval in ms; 0 = disabled for that platform
# Per-platform socket/device selection (optional — inherits from [Energy] if absent)
intel_cpu_poll_interval_ms   = 10
intel_cpu_sockets            = 0        # poll only socket 0, even though [Energy] reads 0,1

amd_cpu_poll_interval_ms     = 10
# amd_cpu_sockets absent → inherits 0,1 from [Energy]

arm_cpu_poll_interval_ms     = 0        # disabled

nvidia_gpu_poll_interval_ms  = 100
nvidia_gpu_devices           = 2        # poll only GPU 2, even though [Energy] reads 0,1,2

amd_gpu_poll_interval_ms     = 200      # poll AMD GPU for power even though
amd_gpu_devices              = 1        # [Energy] has amd_gpu=off

intel_gpu_poll_interval_ms   = 0        # disabled
```

### Index syntax for sockets and devices

Reuses the range notation already in `trace_config_parse.c` for punits, extended with
comma-separated lists:

| Syntax | Meaning |
|--------|---------|
| `0` | single socket or device |
| `0,2` | specific indices |
| `0-3` | inclusive range |
| `0,2-4,6` | mixed list and range |
| `all` | all available (default when key is absent) |

### Config examples

**CPU energy only, no GPU** (pure OpenMP job):
```ini
[Energy]
nvidia_gpu = off
amd_gpu    = off
intel_gpu  = off
```

**GPU energy only, no CPU** (pure GPU application):
```ini
[Energy]
intel_cpu = off
amd_cpu   = off
arm_cpu   = off
```

**Snapshot all sockets and all GPUs, but poll only socket 0 and GPU 2 for power**:
```ini
[Energy]
intel_cpu_sockets  = 0,1
nvidia_gpu_devices = 0,1,2

[Power]
intel_cpu_poll_interval_ms  = 10
intel_cpu_sockets           = 0    # narrower than [Energy]
nvidia_gpu_poll_interval_ms = 100
nvidia_gpu_devices          = 2    # narrower than [Energy]
```

**No AMD GPU energy snapshot, but still poll it for power profile**:
```ini
[Energy]
amd_gpu = off                      # skip at enter/exit

[Power]
amd_gpu_poll_interval_ms = 200     # poll anyway — independent of [Energy]
amd_gpu_devices          = 1
```

**Enter/exit for everything, power profile for nothing** (Feature 1 only, no Feature 2):
```ini
# Compile with -DPINSIGHT_ENERGY=TRUE -DPINSIGHT_POWER=FALSE
# No [Power] section needed
```

---

## Runtime Config Structure

Add to `src/trace_config.h`:

```c
#define MAX_ENERGY_PACKAGES  16
#define MAX_ENERGY_GPU_DEVS  16

typedef struct {
    /* PINSIGHT_ENERGY — enter/exit snapshots */
    int      enabled;              /* 0 = off, 1 = on */
    uint64_t socket_mask;          /* bit N = socket N; populated from [Energy] */

    /* PINSIGHT_POWER — periodic polling (independent of above) */
    int      poll_interval_ms;     /* 0 = disabled; >0 = polling active */
    uint64_t power_socket_mask;    /* bit N = socket N; defaults to socket_mask,
                                      overridden by [Power] intel_cpu_sockets */
} cpu_energy_config_t;

typedef struct {
    /* PINSIGHT_ENERGY */
    int      enabled;
    uint64_t device_mask;          /* populated from [Energy] */

    /* PINSIGHT_POWER */
    int      poll_interval_ms;
    uint64_t power_device_mask;    /* defaults to device_mask,
                                      overridden by [Power] *_devices */
} gpu_energy_config_t;

typedef struct {
    cpu_energy_config_t intel_cpu;
    cpu_energy_config_t amd_cpu;
    cpu_energy_config_t arm_cpu;
    gpu_energy_config_t nvidia_gpu;
    gpu_energy_config_t amd_gpu;
    gpu_energy_config_t intel_gpu;
    int                 tick_ms;   /* GCD of all active poll_interval_ms; 0 = no polling */
} energy_power_config_t;

extern energy_power_config_t energy_power_config;
```

Config parser populates the struct in two passes:

1. **Parse `[Energy]`** — set `enabled`, `socket_mask` / `device_mask`; copy to
   `power_socket_mask` / `power_device_mask` as defaults.
2. **Parse `[Power]`** — set `poll_interval_ms`; override `power_socket_mask` /
   `power_device_mask` where the key is explicitly present.
3. **Compute `tick_ms`** = GCD of all non-zero `poll_interval_ms` values.

A platform with `poll_interval_ms > 0` is polled regardless of `enabled` — the two
features are independent. Enter/exit read loop uses `socket_mask`; poll loop uses
`power_socket_mask`:

```c
/* Enter/exit read — uses socket_mask from [Energy] */
for (int s = 0; s < num_sockets; s++) {
    if (cfg->intel_cpu.socket_mask & (1ULL << s))
        pinsight_energy_read_intel_socket(s, &uj[s]);
}

/* Poll loop — uses power_socket_mask from [Power] (may differ) */
for (int s = 0; s < num_sockets; s++) {
    if (cfg->intel_cpu.power_socket_mask & (1ULL << s))
        pinsight_energy_read_intel_socket(s, &uj[s]);
}
```

`tick_ms` drives the single control thread timer; each platform reads at its own sub-rate:

```c
tick_ms = gcd_of_active_intervals();  /* skip zeros */

tick++;
if (cfg->intel_cpu.poll_interval_ms > 0 &&
    tick % (cfg->intel_cpu.poll_interval_ms / tick_ms) == 0)
    pinsight_energy_read_intel_cpu(&e);

if (cfg->nvidia_gpu.poll_interval_ms > 0 &&
    tick % (cfg->nvidia_gpu.poll_interval_ms / tick_ms) == 0)
    pinsight_energy_read_nvidia_gpu(&e);
```

---

## Feature 1: `PINSIGHT_ENERGY` — Enter/Exit Energy Snapshots

**Goal:** Total application energy and average power with zero runtime overhead.
**Reads:** Exactly two — one at `enter_pinsight_func()`, one at `exit_pinsight_func()`.

### 1.1 CMake options

```cmake
option(PINSIGHT_ENERGY           "Total application energy at enter/exit"      FALSE)
option(PINSIGHT_ENERGY_NVIDIA    "NVIDIA GPU energy via NVML"                  FALSE)
option(PINSIGHT_ENERGY_AMD_GPU   "AMD GPU energy via AMD-SMI"                  FALSE)
option(PINSIGHT_ENERGY_INTEL_GPU "Intel GPU energy via Level-Zero"             FALSE)
option(PINSIGHT_POWER            "Power-over-time profiling via control thread" FALSE)

# PINSIGHT_POWER depends on PINSIGHT_ENERGY
if(PINSIGHT_POWER AND NOT PINSIGHT_ENERGY)
    set(PINSIGHT_ENERGY TRUE)
endif()
```

CPU energy (Intel / AMD / ARM) is auto-detected via sysfs — no separate CMake flag and
no extra library dependency. When `PINSIGHT_ENERGY=TRUE`:
- Add `src/energy.c` to `SOURCE_FILES`
- Find and link `libnvidia-ml` if `PINSIGHT_ENERGY_NVIDIA=TRUE`
- Find and link `libamd_smi` if `PINSIGHT_ENERGY_AMD_GPU=TRUE`
- Find and link `libze_loader` if `PINSIGHT_ENERGY_INTEL_GPU=TRUE`
- Remove `src/rapl.c` / `src/rapl.h` from `SOURCE_FILES`

### 1.2 New file: `src/energy.h`

```c
#ifndef ENERGY_H
#define ENERGY_H

#define MAX_ENERGY_PACKAGES  16
#define MAX_ENERGY_GPU_DEVS  16

typedef struct {
    int      num_cpu_sockets;
    uint64_t cpu_energy_uj[MAX_ENERGY_PACKAGES];  /* µJ per socket; 0 if not measured */
    int      num_gpu_devices;
    uint64_t gpu_energy_mj[MAX_ENERGY_GPU_DEVS];  /* mJ per device; 0 if not measured */
} pinsight_energy_t;

/* Called once at library constructor — discovers sysfs paths, inits GPU libs */
void pinsight_energy_init(void);

/* Called once at library destructor */
void pinsight_energy_fini(void);

/* Read CPU sockets into e->cpu_energy_uj[] using socket_mask ([Energy] selection) */
void pinsight_energy_read_cpu(pinsight_energy_t *e);

/* Read GPU devices into e->gpu_energy_mj[] using device_mask ([Energy] selection) */
void pinsight_energy_read_gpu(pinsight_energy_t *e);

/* Read both CPU and GPU — for enter/exit use (socket_mask / device_mask) */
void pinsight_energy_read(pinsight_energy_t *e);

/* Poll variants — use power_socket_mask / power_device_mask ([Power] selection) */
void pinsight_energy_poll_cpu(pinsight_energy_t *e);
void pinsight_energy_poll_gpu(pinsight_energy_t *e);

#endif
```

### 1.3 New file: `src/energy.c`

#### CPU energy — sysfs discovery (no external library)

**Intel** — powercap interface (kernel ≥ 3.13, Intel Sandy Bridge+):
```
/sys/class/powercap/intel-rapl/intel-rapl:<N>/energy_uj
```
Discovery: iterate N = 0, 1, … until the path does not exist.
Unit: µJ as a 64-bit decimal integer.

**AMD EPYC** — hwmon via `amd_energy` driver (kernel ≥ 5.13 for Zen3):
```
/sys/class/hwmon/hwmon<N>/name           → match "amd_energy"
/sys/class/hwmon/hwmon<N>/energy<M>_input → µJ, 64-bit accumulator, no wraparound
```
Discovery: scan hwmon nodes, match `name == "amd_energy"`, enumerate `energy*_input`.
The `amd_energy` driver accumulates the 32-bit MSR in the kernel — safe for long runs.

**ARM** — hwmon best-effort: same scan, match any hwmon exposing `energy*_input`.
Platform-dependent; skip silently if not found.

Implementation pattern (applies to all CPU types):
```c
/* At init: discover paths into static arrays, respecting socket_mask */
static char cpu_sysfs_paths[MAX_ENERGY_PACKAGES][256];
static int  num_discovered_sockets = 0;

/* At read: fopen/fscanf/fclose per enabled socket — called only twice per run */
void pinsight_energy_read_cpu(pinsight_energy_t *e) {
    e->num_cpu_sockets = num_discovered_sockets;
    for (int s = 0; s < num_discovered_sockets; s++) {
        if (!(energy_power_config.intel_cpu.socket_mask & (1ULL << s))) continue;
        FILE *f = fopen(cpu_sysfs_paths[s], "r");
        if (f) { fscanf(f, "%"SCNu64, &e->cpu_energy_uj[s]); fclose(f); }
    }
}
```

Since this is called exactly twice per run for `PINSIGHT_ENERGY`, `fopen`/`fscanf`/
`fclose` overhead (~5–20 µs per socket) is completely irrelevant. For `PINSIGHT_POWER`
polling, open persistent fds at init and use `read()` for ~100–300 ns per socket.

#### GPU energy — native libraries (optional)

**NVIDIA — NVML** (`PINSIGHT_ENERGY_NVIDIA`):
```c
#include <nvml.h>
/* Init: */ nvmlInit_v2(); nvmlDeviceGetCount(&n); for(i) nvmlDeviceGetHandleByIndex(i,&h[i]);
/* Enter/exit read: */ uint64_t mask = cfg->nvidia_gpu.device_mask;
                       for(i) { if(mask & 1ULL<<i) nvmlDeviceGetTotalEnergyConsumption(h[i],&mj[i]); }
/* Poll read:       */ uint64_t pmask = cfg->nvidia_gpu.power_device_mask;
                       for(i) { if(pmask & 1ULL<<i) nvmlDeviceGetTotalEnergyConsumption(h[i],&mj[i]); }
/* Fini: */ nvmlShutdown();
```
`GetTotalEnergyConsumption` is a monotonically increasing millijoule counter — identical
enter/exit delta semantics as RAPL.

**AMD GPU — AMD-SMI** (`PINSIGHT_ENERGY_AMD_GPU`):
```c
#include <amd_smi/amdsmi.h>
/* Init: */ amdsmi_init(AMDSMI_INIT_AMD_GPUS); amdsmi_get_processor_handles(...);
/* Enter/exit read: */ uint64_t mask = cfg->amd_gpu.device_mask; uint64_t res, ts;
                       for(i) { if(mask & 1ULL<<i) amdsmi_get_energy_count(h[i],&mj[i],&res,&ts); }
/* Poll read:       */ uint64_t pmask = cfg->amd_gpu.power_device_mask;
                       for(i) { if(pmask & 1ULL<<i) amdsmi_get_energy_count(h[i],&mj[i],&res,&ts); }
/* Fini: */ amdsmi_shut_down();
```
**MI300A note:** `amdsmi_get_energy_count` returns combined CPU+GPU+HBM package energy —
hardware does not expose per-component breakdown. Tracepoint field should be labeled
`apu_package_energy_mj`.

**Intel GPU — Level-Zero** (`PINSIGHT_ENERGY_INTEL_GPU`):
```c
#include <level_zero/zes_api.h>
/* Enter/exit read: */ uint64_t mask = cfg->intel_gpu.device_mask;
                       zes_power_energy_counter_t ctr;
                       for(i) { if(mask & 1ULL<<i) zesPowerGetEnergyCounter(ph[i],&ctr); uj[i]=ctr.energy; }
/* Poll read:       */ uint64_t pmask = cfg->intel_gpu.power_device_mask;
                       for(i) { if(pmask & 1ULL<<i) zesPowerGetEnergyCounter(ph[i],&ctr); uj[i]=ctr.energy; }
```

### 1.4 New LTTng tracepoint provider: `energy_pinsight_lttng_ust`

New file: `src/energy_lttng_ust_tracepoint.h`. Separate provider — decoupled from OpenMP,
MPI, CUDA, HIP, and Python providers. Can be enabled/disabled in LTTng independently.

Three tracepoints share the same field layout:

```c
/* Fields carried by energy_enter, energy_exit, and energy_sample */
LTTNG_UST_TP_FIELDS(
    COMMON_LTTNG_UST_TP_FIELDS_GLOBAL          /* pid, hostname, timestamp */
    /* CPU — one field per socket slot, 0 if not measured */
    lttng_ust_field_integer(uint64_t, cpu0_uj, cpu0_uj)
    lttng_ust_field_integer(uint64_t, cpu1_uj, cpu1_uj)
    lttng_ust_field_integer(uint64_t, cpu2_uj, cpu2_uj)
    lttng_ust_field_integer(uint64_t, cpu3_uj, cpu3_uj)
    /* GPU — one field per device slot, 0 if not measured */
    lttng_ust_field_integer(uint64_t, gpu0_mj, gpu0_mj)
    lttng_ust_field_integer(uint64_t, gpu1_mj, gpu1_mj)
    lttng_ust_field_integer(uint64_t, gpu2_mj, gpu2_mj)
    lttng_ust_field_integer(uint64_t, gpu3_mj, gpu3_mj)
    /* PINSIGHT_POWER only — 0 in energy_enter/exit */
    lttng_ust_field_integer(uint64_t, poll_seq, poll_seq)
)
```

Fixed fields up to 4 sockets and 4 GPU devices — extend if needed. Unmeasured slots carry
0, which the analysis tool uses to skip them. `poll_seq` is a monotonic counter for
ordering samples and detecting missed ticks.

### 1.5 Wire into `enter_exit.c`

```c
void enter_pinsight_func() {
    pid = getpid(); gethostname(hostname, 48);
#ifdef PINSIGHT_ENERGY
    pinsight_energy_init();
    pinsight_energy_t e = {0};
    pinsight_energy_read(&e);
    lttng_ust_tracepoint(energy_pinsight_lttng_ust, energy_enter,
        e.cpu_energy_uj[0], e.cpu_energy_uj[1], e.cpu_energy_uj[2], e.cpu_energy_uj[3],
        e.gpu_energy_mj[0], e.gpu_energy_mj[1], e.gpu_energy_mj[2], e.gpu_energy_mj[3], 0);
#endif
    lttng_ust_tracepoint(pinsight_enter_exit_lttng_ust, enter_pinsight);
    pinsight_control_thread_start();
    pinsight_install_signal_handler();
#ifdef PINSIGHT_CUDA
    LTTNG_CUPTI_Init();
#endif
#ifdef PINSIGHT_HIP
    LTTNG_ROCTRACER_Init();
#endif
}

void exit_pinsight_func() {
    lttng_ust_tracepoint(pinsight_enter_exit_lttng_ust, exit_pinsight);
#ifdef PINSIGHT_CUDA
    LTTNG_CUPTI_Fini();
#endif
#ifdef PINSIGHT_HIP
    LTTNG_ROCTRACER_Fini();
#endif
    pinsight_control_thread_stop();
#ifdef PINSIGHT_ENERGY
    pinsight_energy_t e = {0};
    pinsight_energy_read(&e);
    lttng_ust_tracepoint(energy_pinsight_lttng_ust, energy_exit,
        e.cpu_energy_uj[0], e.cpu_energy_uj[1], e.cpu_energy_uj[2], e.cpu_energy_uj[3],
        e.gpu_energy_mj[0], e.gpu_energy_mj[1], e.gpu_energy_mj[2], e.gpu_energy_mj[3], 0);
    pinsight_energy_fini();
#endif
}
```

`energy_enter` fires before `enter_pinsight` (baseline precedes app-start marker).
`energy_exit` fires after `exit_pinsight` (app-end marker precedes final snapshot).
The energy delta slightly over-counts PInsight teardown overhead — negligible.

### 1.6 Clean up old energy code

- Delete `src/rapl.c` and `src/rapl.h`
- Remove `ENERGY_LTTNG_UST_TP_ARGS`, `ENERGY_LTTNG_UST_TP_FIELDS`,
  `ENERGY_LTTNG_UST_TRACEPOINT_CALL_ARGS` macros from `src/ompt_lttng_ust_tracepoint.h`
- Remove all `#ifdef PINSIGHT_ENERGY` blocks from `src/ompt_callback.c`
- Remove `package_energy` global variable from `src/ompt_callback.h` / `ompt_callback.c`

### 1.7 Analysis — what `PINSIGHT_ENERGY` gives you

```python
enter = trace.find_event("energy_pinsight_lttng_ust:energy_enter")
exit  = trace.find_event("energy_pinsight_lttng_ust:energy_exit")
duration_s = (exit.timestamp - enter.timestamp) / 1e9

for s in range(4):
    delta_uj = exit[f"cpu{s}_uj"] - enter[f"cpu{s}_uj"]
    if delta_uj > 0:
        print(f"CPU socket {s}: {delta_uj/1e6:.3f} J, avg {delta_uj/1e6/duration_s:.2f} W")

for d in range(4):
    delta_mj = exit[f"gpu{d}_mj"] - enter[f"gpu{d}_mj"]
    if delta_mj > 0:
        print(f"GPU device {d}: {delta_mj/1e3:.3f} J, avg {delta_mj/1e3/duration_s:.2f} W")
```

### 1.8 `PINSIGHT_ENERGY` file change summary

| Action | File |
|--------|------|
| **New** | `src/energy.h` |
| **New** | `src/energy.c` |
| **New** | `src/energy_lttng_ust_tracepoint.h` |
| **Modified** | `src/enter_exit.c` — wire init/read/fini + tracepoint calls |
| **Modified** | `CMakeLists.txt` — add all energy/power options, remove rapl, add GPU lib finds |
| **Modified** | `src/trace_config.h` — add `energy_power_config_t` |
| **Modified** | `src/trace_config_parse.c` — parse `[Energy]` section |
| **Modified** | `src/ompt_lttng_ust_tracepoint.h` — remove `ENERGY_LTTNG_*` macros |
| **Modified** | `src/ompt_callback.c` — remove all `#ifdef PINSIGHT_ENERGY` blocks |
| **Deleted** | `src/rapl.c` |
| **Deleted** | `src/rapl.h` |

---

## Feature 2: `PINSIGHT_POWER` — Periodic Energy Snapshots via the Control Thread

**Goal:** Power-over-time profile — see how power varies across application phases.
**Mechanism:** The existing control thread polls energy at configurable per-platform
intervals and emits `energy_sample` tracepoints. Domain callbacks (OMPT, HIP, MPI,
Python) remain completely energy-free. Analysis correlates energy samples with domain
events by timestamp.

`PINSIGHT_POWER` reuses `energy.h` / `energy.c` from `PINSIGHT_ENERGY` unchanged.
No additional library dependencies.

### 2.1 CMake option

```cmake
option(PINSIGHT_POWER  "Power-over-time profiling via control thread polling"  FALSE)
```

### 2.2 Hardcoded interval defaults

Defined in `src/energy.h` — applied when the `[Power]` key is absent from the config:

```c
/* Default poll intervals — set at 10× hardware update rate to avoid redundant reads */
#define PINSIGHT_POWER_INTERVAL_INTEL_CPU_MS    10   /* RAPL updates ~1 ms */
#define PINSIGHT_POWER_INTERVAL_AMD_CPU_MS      10
#define PINSIGHT_POWER_INTERVAL_ARM_CPU_MS      50   /* platform-dependent */
#define PINSIGHT_POWER_INTERVAL_NVIDIA_GPU_MS  100   /* energy counter ~10 ms */
#define PINSIGHT_POWER_INTERVAL_AMD_GPU_MS     200   /* AMD-SMI higher read cost */
#define PINSIGHT_POWER_INTERVAL_INTEL_GPU_MS   100
```

### 2.3 Control thread — single timer, per-platform sub-rates

The control thread runs **one** `sem_timedwait` loop at `tick_ms` = GCD of all active
poll intervals. Each platform reads at its own sub-rate using tick modulus:

```c
static void *pinsight_control_loop(void *arg) {
    /* ... existing SIGUSR1 mask setup ... */
    uint64_t tick = 0;

    while (!control_shutdown) {
#ifdef PINSIGHT_POWER
        int power_active = (energy_power_config.tick_ms > 0);
#else
        int power_active = 0;
#endif
        if (power_active) {
            struct timespec deadline;
            clock_gettime(CLOCK_REALTIME, &deadline);
            long ns = (long)energy_power_config.tick_ms * 1000000L
                    + deadline.tv_nsec;
            deadline.tv_sec  += ns / 1000000000L;
            deadline.tv_nsec  = ns % 1000000000L;

            int ret = sem_timedwait(&control_sem, &deadline);
            if (ret == -1 && errno == ETIMEDOUT) {
                pinsight_control_energy_sample(++tick);
                continue;
            }
            /* sem_post: fall through to handle control event */
        } else {
            while (sem_wait(&control_sem) == -1 && errno == EINTR) {}
        }
        /* ... existing: SIGUSR1 reload, mode change, INTROSPECT ... */
    }
}
```

Per-platform read helper:

```c
static void pinsight_control_energy_sample(uint64_t tick) {
    energy_power_config_t *cfg = &energy_power_config;
    int tick_ms = cfg->tick_ms;
    pinsight_energy_t e = {0};

#define SHOULD_POLL(platform_ms) \
    ((platform_ms) > 0 && (tick % ((platform_ms) / tick_ms) == 0))

    /* poll_cpu/gpu variants use power_socket_mask / power_device_mask,
       which may differ from the socket_mask / device_mask used at enter/exit */
    if (SHOULD_POLL(cfg->intel_cpu.poll_interval_ms))   pinsight_energy_poll_intel_cpu(&e);
    if (SHOULD_POLL(cfg->amd_cpu.poll_interval_ms))     pinsight_energy_poll_amd_cpu(&e);
    if (SHOULD_POLL(cfg->arm_cpu.poll_interval_ms))     pinsight_energy_poll_arm_cpu(&e);
    if (SHOULD_POLL(cfg->nvidia_gpu.poll_interval_ms))  pinsight_energy_poll_nvidia_gpu(&e);
    if (SHOULD_POLL(cfg->amd_gpu.poll_interval_ms))     pinsight_energy_poll_amd_gpu(&e);
    if (SHOULD_POLL(cfg->intel_gpu.poll_interval_ms))   pinsight_energy_poll_intel_gpu(&e);

    lttng_ust_tracepoint(energy_pinsight_lttng_ust, energy_sample,
        e.cpu_energy_uj[0], e.cpu_energy_uj[1],
        e.cpu_energy_uj[2], e.cpu_energy_uj[3],
        e.gpu_energy_mj[0], e.gpu_energy_mj[1],
        e.gpu_energy_mj[2], e.gpu_energy_mj[3],
        tick);
}
```

For `PINSIGHT_POWER`, CPU sysfs reads should use **persistent fds** (opened at
`pinsight_energy_init()`, `read()` per poll tick) rather than `fopen`/`fclose` — the
read cost matters at ~10 ms intervals, unlike the twice-per-run `PINSIGHT_ENERGY` reads.

### 2.4 SIGUSR1 reload interaction

On config reload: re-parse `[Energy]` and `[Power]` sections, update
`energy_power_config`, recompute `tick_ms = GCD(all active intervals)`. The next loop
iteration picks up the new `tick_ms` naturally — enabling or disabling platforms, changing
intervals, or changing socket/device masks all take effect without restart.

### 2.5 Analysis — what `PINSIGHT_POWER` gives you

`energy_sample` events are interleaved with OMPT/HIP/MPI/Python events on the same LTTng
timeline. Per-phase power attribution by linear interpolation:

```python
samples = trace.find_events("energy_pinsight_lttng_ust:energy_sample")
regions = trace.find_pairs("ompt_pinsight_lttng_ust:parallel_begin",
                           "ompt_pinsight_lttng_ust:parallel_end")

for region in regions:
    before = last_sample_before(samples, region.t_begin)
    after  = first_sample_after(samples, region.t_end)
    fraction = (region.t_end - region.t_begin) / (after.ts - before.ts)
    energy_J = (after.cpu0_uj - before.cpu0_uj) / 1e6 * fraction
    power_W  = energy_J / ((region.t_end - region.t_begin) / 1e9)
```

### 2.6 `PINSIGHT_POWER` file change summary

| Action | File |
|--------|------|
| **Modified** | `src/energy.h` — add `pinsight_energy_poll_cpu/gpu()` using `power_socket/device_mask`; add interval defaults |
| **Modified** | `src/energy.c` — implement poll variants + persistent fds for CPU sysfs polling path |
| **Modified** | `src/energy_lttng_ust_tracepoint.h` — add `energy_sample` tracepoint |
| **Modified** | `src/pinsight_control_thread.c` — add timed loop + `energy_sample` emission |
| **Modified** | `src/trace_config.h` — add poll intervals to `energy_power_config_t` |
| **Modified** | `src/trace_config_parse.c` — parse `[Power]` section + compute `tick_ms` |

---

## Platform Support Summary

| Platform | sysfs / Library | Hardware update rate | Default poll interval | CMake flag |
|----------|-----------------|---------------------|-----------------------|------------|
| Intel CPU | `/sys/class/powercap/intel-rapl/*/energy_uj` | ~1 ms | 10 ms | `PINSIGHT_ENERGY` |
| AMD CPU (EPYC) | `/sys/class/hwmon/hwmon*/energy*_input` (amd_energy) | ~1 ms | 10 ms | `PINSIGHT_ENERGY` |
| ARM CPU | `/sys/class/hwmon/hwmon*/energy*_input` (best effort) | ~10–50 ms | 50 ms | `PINSIGHT_ENERGY` |
| NVIDIA GPU | `nvmlDeviceGetTotalEnergyConsumption` | ~10 ms | 100 ms | `PINSIGHT_ENERGY_NVIDIA` |
| AMD GPU / MI300A | `amdsmi_get_energy_count` | ~10 ms | 200 ms | `PINSIGHT_ENERGY_AMD_GPU` |
| Intel GPU | `zesPowerGetEnergyCounter` | ~10 ms | 100 ms | `PINSIGHT_ENERGY_INTEL_GPU` |

**MI300A note:** AMD-SMI reports combined CPU+GPU+HBM package energy — hardware does not
expose per-component breakdown. Tracepoint field labeled `apu_package_energy_mj`.

---

## Implementation Order

### `PINSIGHT_ENERGY` steps

1. Write `src/energy.h` + `src/energy.c` — Intel CPU sysfs path only, no GPU.
   Test on Intel Linux with `-DPINSIGHT_ENERGY=TRUE`. Verify `energy_enter` /
   `energy_exit` appear in `lttng view` with correct µJ values.
2. Add AMD CPU hwmon path. Test on an EPYC system.
3. Remove old RAPL code: delete `src/rapl.c` / `src/rapl.h`, remove `ENERGY_LTTNG_*`
   macros from `ompt_lttng_ust_tracepoint.h`, remove energy blocks from `ompt_callback.c`.
4. Add `[Energy]` config parsing in `trace_config_parse.c` — enable/disable per platform,
   socket/device index list → bitmask conversion.
5. Add NVIDIA GPU path + CMake find for NVML. Test on a CUDA system.
6. Add AMD GPU path + CMake find for AMD-SMI. Test on El Capitan after ROCTracer
   validation.
7. Add Intel GPU path + CMake find for Level-Zero (lower priority).

### `PINSIGHT_POWER` steps

8. Add per-platform interval defaults to `src/energy.h`. Add persistent fd path in
   `src/energy.c` for CPU sysfs reads used during polling.
9. Add `[Power]` section parsing in `trace_config_parse.c`. Implement `tick_ms` GCD
   computation.
10. Add `energy_sample` tracepoint. Extend control thread with timed loop. Test with
    `intel_cpu_poll_interval_ms = 1000`, verify samples appear at ~1 s intervals.
11. Implement analysis-side linear interpolation in the Python analysis tool.

---

## Supplementary Details

Details that are too low-level for the main plan but needed for correct implementation.

### GCD helper and tick_ms edge cases

```c
static int gcd(int a, int b) { return b == 0 ? a : gcd(b, a % b); }

static int compute_tick_ms(energy_power_config_t *cfg) {
    int intervals[] = {
        cfg->intel_cpu.poll_interval_ms,
        cfg->amd_cpu.poll_interval_ms,
        cfg->arm_cpu.poll_interval_ms,
        cfg->nvidia_gpu.poll_interval_ms,
        cfg->amd_gpu.poll_interval_ms,
        cfg->intel_gpu.poll_interval_ms,
    };
    int result = 0;
    for (int i = 0; i < 6; i++) {
        if (intervals[i] > 0)
            result = (result == 0) ? intervals[i] : gcd(result, intervals[i]);
    }
    return result;  /* 0 means no polling active — control thread uses sem_wait */
}
```

When `tick_ms == 0`, the control thread stays on the original blocking `sem_wait` path —
no periodic wakeups, zero overhead, identical to `PINSIGHT_POWER=FALSE`.

### Persistent fd pattern for CPU sysfs polling

For `PINSIGHT_POWER`, each sysfs energy file is opened once at `pinsight_energy_init()`
and held open. A `read()` on a sysfs file fd resets the file offset and returns the
current counter value — the same as `fopen`/`fscanf`/`fclose` but without the open/close
overhead:

```c
static int cpu_poll_fds[MAX_ENERGY_PACKAGES];  /* opened at init, -1 if inactive */

/* At init (for each enabled socket): */
cpu_poll_fds[s] = open(cpu_sysfs_paths[s], O_RDONLY);

/* At each poll tick: */
void pinsight_energy_poll_intel_cpu(pinsight_energy_t *e) {
    char buf[32];
    for (int s = 0; s < num_discovered_sockets; s++) {
        if (!(cfg->intel_cpu.power_socket_mask & (1ULL << s))) continue;
        if (cpu_poll_fds[s] < 0) continue;
        lseek(cpu_poll_fds[s], 0, SEEK_SET);       /* rewind sysfs file */
        int n = read(cpu_poll_fds[s], buf, sizeof(buf) - 1);
        if (n > 0) { buf[n] = '\0'; e->cpu_energy_uj[s] = strtoull(buf, NULL, 10); }
    }
}

/* At fini: */
for (int s = 0; s < MAX_ENERGY_PACKAGES; s++)
    if (cpu_poll_fds[s] >= 0) { close(cpu_poll_fds[s]); cpu_poll_fds[s] = -1; }
```

The `lseek(fd, 0, SEEK_SET)` + `read()` pattern is the standard way to re-read a sysfs
counter without reopening the file. Cost: ~1–3 µs per socket, acceptable at 10 ms intervals.

### Config parser two-pass logic (pseudocode)

```c
void parse_energy_section(const char *key, const char *value) {
    if      (strcmp(key, "intel_cpu")          == 0) cfg.intel_cpu.enabled       = parse_onoff(value);
    else if (strcmp(key, "intel_cpu_sockets")  == 0) cfg.intel_cpu.socket_mask   = parse_index_list(value);
    else if (strcmp(key, "amd_cpu")            == 0) cfg.amd_cpu.enabled         = parse_onoff(value);
    else if (strcmp(key, "amd_cpu_sockets")    == 0) cfg.amd_cpu.socket_mask     = parse_index_list(value);
    /* ... nvidia_gpu, amd_gpu, intel_gpu ... */
}

void parse_power_section(const char *key, const char *value) {
    if      (strcmp(key, "intel_cpu_poll_interval_ms") == 0) cfg.intel_cpu.poll_interval_ms  = atoi(value);
    else if (strcmp(key, "intel_cpu_sockets")          == 0) cfg.intel_cpu.power_socket_mask = parse_index_list(value);
    /* ... etc ... */
}

/* After both sections parsed: */
void energy_power_config_finalize(void) {
    /* Inherit socket/device masks from [Energy] where [Power] didn't override */
    if (cfg.intel_cpu.power_socket_mask == 0 && cfg.intel_cpu.poll_interval_ms > 0)
        cfg.intel_cpu.power_socket_mask = cfg.intel_cpu.socket_mask;
    /* ... same for all platforms ... */
    cfg.tick_ms = compute_tick_ms(&cfg);
}
```

Call `energy_power_config_finalize()` after parsing the entire config file, not after
each section — both sections must be fully parsed before resolving defaults.

### `parse_index_list` — converting "0,2-4" to a bitmask

```c
uint64_t parse_index_list(const char *s) {
    /* "all" or absent → all bits set */
    if (strcmp(s, "all") == 0) return ~0ULL;
    uint64_t mask = 0;
    /* tokenize by comma, then handle "N" or "N-M" per token */
    char buf[64]; strncpy(buf, s, sizeof(buf));
    char *tok = strtok(buf, ",");
    while (tok) {
        char *dash = strchr(tok, '-');
        if (dash) {
            int lo = atoi(tok), hi = atoi(dash + 1);
            for (int i = lo; i <= hi && i < 64; i++) mask |= (1ULL << i);
        } else {
            int idx = atoi(tok);
            if (idx >= 0 && idx < 64) mask |= (1ULL << idx);
        }
        tok = strtok(NULL, ",");
    }
    return mask;
}
```

Check whether the existing punit range parser in `trace_config_parse.c` already does
this — if so, reuse it directly rather than reimplementing.

### Default socket/device mask at `pinsight_energy_init()`

When no `[Energy]` config has been parsed (or a platform key is absent), the mask should
default to "all discovered sockets/devices":

```c
/* After sysfs discovery finds num_discovered_sockets = N: */
if (cfg->intel_cpu.socket_mask == 0)           /* not set by config */
    cfg->intel_cpu.socket_mask = (1ULL << N) - 1;  /* all N sockets */
```

Apply the same defaulting for GPU device masks after NVML/AMD-SMI enumeration.
