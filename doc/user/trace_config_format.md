# PInsight Trace Configuration Format

PInsight uses an enhanced INI-style configuration file to control tracing behavior at runtime.

## File Structure

The configuration file consists of sections. Each section targets a specific tracing scope, which could be a Domain or a Code Region/Lexgion. A domain, such as OpenMP, MPI, or CUDA, defines a set of events (omp_parallel_begin, omp_parallel_end, etc.) and a group of parallel units (e.g. team, thread, device for OpenMP). A code region/lexgion enclose a specific piece of code, e.g. a function, a parallel region. 

### Section Header Format

```ini
[ACTION Target] : Inheritance : PunitSets
```

#### 1. ACTION (Optional)
Specifies how the new configuration interacts with the existing configuration. There are three actions:

- **SET** (Default): Merge new settings with existing configuration. Only the fields explicitly specified in the section body are modified; all other fields retain their current values. If no configuration exists for the target, a new one is created.
- **RESET**: Revert the target to its computed/system defaults. No section body is needed. Only valid for `*.default` sections (see validity table below).
- **REMOVE**: Delete or disable the configuration for the target. No section body is needed. Only valid for non-default sections. Supports **wildcard removal**: `[REMOVE Domain.PunitKind(*)]` removes all configs of that punit kind.

#### 2. Target
Specifies what is being configured. There are five types of targets:
- **`Domain.default`**: Domain-wide settings and defaults — trace mode (`trace_mode`), punit scope (`Domain.PunitKind = (Range)`), per-event on/off, plus node-policy keys such as `device_activity` (HIP/CUDA). E.g., `OpenMP.default`, `HIP.default`.
- **`Domain.PunitKind(PunitSet)`**: Configuration for a subset of parallel units (e.g., `OpenMP.team(0-3, 7, 12-20)`, `MPI.rank(0-4)`).
- **`Lexgion.default`**: Default configuration for all code regions across all domains.
- **`Lexgion(Domain).default`**: Default lexgion configuration for a specific domain (e.g., `Lexgion(OpenMP).default`). Eagerly initialized as `Lexgion.default ⊕ Domain.default` (rate triple from global default, events from domain default).
- **`Lexgion(Address)` or `Lexgion(Addr1, Addr2, ...)`**: Configuration for one or more specific code regions by address (e.g., `Lexgion(0x400500)` or `Lexgion(0x400500, 0x400600)`). When multiple addresses are listed, each gets its own config but shares the same section body settings.

> [!IMPORTANT]
> Sections should appear in the config file in the order listed above: `Domain.default` → `Domain.PunitKind(Set)` → `Lexgion.default` → `Lexgion(Domain).default` → `Lexgion(Address)`. This is required because all inheritance is resolved at parse time as a snapshot copy of the referenced target's current state. A section that inherits from or depends on a target defined later in the file will see stale or uninitialized defaults.


##### Action-Target Validity

| Target Type | SET | RESET | REMOVE |
|---|---|---|---|
| `Domain.default` | ✅ Merge settings | ✅ Revert to system install defaults | ❌ Invalid (use RESET) |
| `Lexgion.default` | ✅ Merge settings | ✅ Revert to system defaults | ❌ Invalid (use RESET) |
| `Lexgion(Domain).default` | ✅ Merge settings | ✅ Revert to `Lexgion.default ⊕ Domain.default` | ❌ Invalid (use RESET) |
| `Domain.PunitKind(Set)` | ✅ Merge settings | ❌ Invalid (use REMOVE) | ✅ Clear/disable this punit-specific config so they will use default settings in the future |
| `Domain.PunitKind(*)` | ❌ Invalid | ❌ Invalid | ✅ Remove ALL configs of this punit kind so they will use default settings in the future |
| `Lexgion(Address)` | ✅ Merge settings | ❌ Invalid (use REMOVE) | ✅ Mark lexgion-specific config as removed so they will use default settings in the future |

##### RESET Semantics by Target

| Target | RESET reverts to... |
|---|---|
| `Domain.default` | System install defaults: mode = TRACING if domain has registered events (OFF otherwise), events as registered by the domain. Punit ranges are not reverted. |
| `Lexgion.default` | System install defaults: `tracing_rate=1`, `trace_starts_at=0`, `max_num_traces=-1`, all event overrides cleared |
| `Lexgion(Domain).default` | Computed default: rate triple from `Lexgion.default` + events from `Domain.default` |

#### 3. Inheritance (Optional)
Specifies domain defaults to inherit events from, separated by commas. **Applies to all `Lexgion(*)` section types** (including `Lexgion.default` and `Lexgion(Domain).default`) and `Domain.PunitKind(Set)` sections. Does **not** apply to `Domain.default`.
- Example: `OpenMP.default, MPI.default`

> **Note:** Inheritance is applied at parse time as a **snapshot copy**. If a default section is modified later in the configuration file, sections that already inherited from it (earlier in the file) are **not** retroactively updated. Only sections defined after the change will see the new defaults.

#### 4. PunitSet (Optional)
Additional punit constraints from other domains, separated by commas. **Only applies to `Domain.PunitKind(Set)` and `Lexgion(Address)` sections** — Lexgion default sections do not use PunitSet.
- Format: `Domain.PunitKind(PunitSet)`
- Example: `MPI.rank(0-3, 8-15, 63), CUDA.device(0)`

### Section Body (Key-Value Pairs)

#### Domain Default Configuration (`Domain.default`)
- **Trace Mode**: `trace_mode = OFF|STANDBY|MONITORING|TRACING` — the domain-wide operating mode. Domain-wide only: not valid in `Domain.PunitKind(Set)` sections (a punit subset cannot have its own mode).
- **Punit Scope**: `Domain.PunitKind = (Range)` (e.g., `OpenMP.thread = (0-15)`) — which parallel units are traced. Domain-wide only. (Event names never contain a dot, so a dotted key is always a punit-scope key.)
- **Event Control**: `EventName = on|off`
- **GPU activity node-policy** (HIP/CUDA only): `device_activity = off | on | anyone_per_node | leader_per_node | rotate_per_node:<ms>` — controls *which ranks* open the GPU activity pool (ROCTracer/CUPTI). Default **`off`**. See **Node-Policy Keys** below.

#### Node-Policy Keys (`device_activity`, energy `measure`)

A *node-policy* selects **which ranks perform a per-process capability measurement** — the
GPU activity pool (`device_activity`, HIP/CUDA) or energy (`measure`, `[Energy]`). It is
resolved **once at startup (per-run)** and is **not windowed** (use `trace_mode` for
per-window on/off). Full design: `doc/node_singleton_measurement_design.md`.

Values (case-insensitive):

| Value | Meaning |
|---|---|
| `off` | disabled |
| `on` | every rank measures/collects |
| `anyone_per_node` | exactly one **arbitrary** rank/node (elected via a node-local `flock` file) |
| `leader_per_node` | exactly one **deterministic** rank/node — the node leader (`PINSIGHT_NODE_LEADER` env → local-rank env `FLUX_TASK_LOCAL_ID`/`SLURM_LOCALID`/`OMPI_COMM_WORLD_LOCAL_RANK`/`MPI_LOCALRANKID` → MPI `Comm_split_type` → lock fallback) |
| `rotate_per_node:<ms>` | one **rotating** collector/node, cycling every `<ms>`. **Phase 3 — not yet implemented; currently falls back to `leader_per_node`.** `device_activity` only. |

- **`device_activity`** gates **only** the GPU activity pool — **host-side HIP/CUDA tracing
  stays on for all ranks** regardless. Default **`off`** (activity is the expensive GPU-side
  path, now opt-in). `off` keeps host launch/memcpy/sync events but drops kernel/memcpy
  *activity* (GPU-exec) records; `anyone_per_node`/`leader_per_node` collect on exactly one
  GPU/node (≈¼ the overhead at 4 ranks/node, at the cost of 1 GPU/node of activity coverage).
  Set in `[HIP.default]` / `[CUDA.default]`. **Replaces the removed
  `PINSIGHT_HIP_DISABLE_ACTIVITY` env** (which was equivalent to `device_activity = off`).
- **Implementation status:** `off`/`on`/`anyone_per_node`/`leader_per_node` implemented and
  hardware-validated (Phase 1). `rotate_per_node` parses but is not yet active.

#### `[Energy]` Section

Energy/power is a measurement **service** — *not* a domain (no modes, lexgions, or punits),
so it uses a plain `[Energy]` section. The only runtime key today is `measure`:

```ini
[Energy]
    measure = off | on | anyone_per_node | leader_per_node
```
- Selects **which ranks measure energy** (same node-policy values; `rotate_per_node` is
  **not** valid for energy). Default **`off`** (since 2026-07-31 — energy is
  opt-in; earlier builds defaulted to `on`).
  `leader_per_node`/`anyone_per_node` → exactly one rank/node measures, yielding one clean
  node-energy series (fixes multi-rank multi-counting of the shared node counters).
- Also settable via env **`PINSIGHT_MEASURE_ENERGY`** (`ON|OFF|ANYONE_PER_NODE|LEADER_PER_NODE`),
  which **overrides** the config key.
- **Limitation:** per-platform enables and socket/device masks are future work
  (`doc/energy_power_implementation_plan.md`); any other `[Energy]` keys are currently ignored.

#### Lexgion Configuration (applies to `Lexgion.default`, `Lexgion(Domain).default`, and `Lexgion(Address)`)
- **Tracing Rate**: `tracing_rate = N` (Trace 1 out of N executions)
- **Max Traces**: `max_num_traces = N`
- **Start Delay**: `trace_starts_at = N`

> **Tracing window.** A *window* is a stretch of execution during which a domain
> stays in one mode (TRACING / MONITORING / STANDBY / OFF). A TRACING window ends —
> and `window_end_action` transitions the domain — when **either** the count trigger
> (`max_num_traces`, per `window_end_trigger`) **or** the time trigger
> (`window_timeout`, wall-clock seconds) fires, whichever comes first.

- **Auto Mode Switch**: `window_end_action = MODE` — Action performed when the TRACING window ends (count trigger and/or `window_timeout`). Supports:
  - Shorthand: `window_end_action = MONITORING` (applies to all domains with events in the lexgion)
  - Explicit per-domain: `window_end_action = OpenMP:MONITORING, MPI:OFF`
  - **INTROSPECT action**: `window_end_action = INTROSPECT:pause:script[:resume_mode]`
    - `pause` — seconds to pause the app before auto-resuming (the INTROSPECT *pause duration*; internally `introspect_pause_duration`). Semantics:
      - `> 0`: pause for N seconds (interruptible by SIGUSR1)
      - `0`: no pause — run the script and immediately continue
      - `-1`: pause indefinitely — only SIGUSR1 resumes the app
    - `script` — analysis script to launch (`-` = none). Receives `<chunk_path> <app_pid> <config_file>` as arguments.
    - `resume_mode` — domain mode after resume (default: `MONITORING`). Accepts `OFF`, `STANDBY`, `MONITORING`, or `TRACING`.
    - When INTROSPECT fires, PInsight automatically runs `lttng rotate` to flush traces, then optionally launches the script, then pauses ALL application threads (via the control thread) until the pause duration elapses or SIGUSR1.
- **Window Timeout**: `window_timeout = N` — End the TRACING window after **N wall-clock seconds** (integer) and perform `window_end_action`, **even if no region reaches `max_num_traces`**. `0`/absent/negative = disabled (default). This is a single per-process backstop handled entirely in the control thread (zero application overhead); the clock starts at process start. Use it standalone for time-windowed capture ("trace the first N seconds, then drop to MONITORING"), or alongside a count trigger as a guaranteed ceiling on TRACING duration.
- **Count Policy**: `window_end_trigger = first | all` — Which count condition ends the window (default `first`):
  - `first` — the first region to reach `max_num_traces` fires (lowest overhead; biased coverage).
  - `all` — fire only once **every** region this thread has encountered has capped (per-thread; the first thread to satisfy it fires). ⚠ If a region is rare or never re-reached the window may never end via count alone — pair `all` with a `window_timeout` backstop (PInsight warns at startup if you don't).
  - `anchor` — **reserved, not yet implemented** (the parser rejects it and falls back to `first`).
- **Event Control**: `EventName = on|off`

---

## Examples

### 1. Setting Domain-Wide Configuration
Set trace mode and punit scope for the OpenMP domain.
```ini
[OpenMP.default]
    trace_mode = TRACING
    OpenMP.team = (0-4)
    OpenMP.thread = (0, 15)
```

### 2. Disabling a Domain at Runtime
Send `kill -USR1 <pid>` after editing the config file to disable OpenMP tracing with zero overhead.
```ini
[OpenMP.default]
    trace_mode = OFF
```

### 2b. STANDBY Mode (Recoverable Low-Overhead)
Callbacks remain registered but return immediately. Unlike OFF (permanent), STANDBY can be switched back to TRACING via `kill -USR1`.
```ini
[OpenMP.default]
    trace_mode = STANDBY
```

### 3. Setting Domain Event Configuration
Merge new event settings with existing OpenMP configuration.
```ini
[OpenMP.default]
    omp_task_create = on
```

### 4. Resetting a Domain to Install Defaults
Revert OpenMP mode and event configuration back to system install defaults (mode = TRACING if events are registered).
```ini
[RESET OpenMP.default]
```

### 5. Adding Specific Thread Tracing
Set tracing config for threads 0-3 without affecting other threads.
```ini
[OpenMP.thread(0-3)] : OpenMP.default
    omp_task_schedule = on
```

### 6. Setting Domain-Specific Lexgion Defaults
Set default tracing behavior for all OpenMP lexgions.
```ini
[Lexgion(OpenMP).default]
    omp_thread_begin = on
    omp_thread_end = on
    omp_task_create = on
    omp_task_schedule = on
    trace_starts_at = 0
    max_num_traces = 200
    tracing_rate = 1
```

### 7. Resetting a Domain-Specific Lexgion Default
Revert to computed default (`Lexgion.default ⊕ OpenMP.default`).
```ini
[RESET Lexgion(OpenMP).default]
```

### 8. Removing a Lexgion Trace
Stop tracing a specific code region.
```ini
[REMOVE Lexgion(0x4010bd)]
```

### 9. Configuring Multiple Lexgions at Once
Apply the same settings to multiple code regions.
```ini
[Lexgion(0x400500, 0x400600, 0x400700)]
    max_num_traces = 100
    tracing_rate = 5
```

### 10. Removing Multiple Lexgions at Once
```ini
[REMOVE Lexgion(0x400500, 0x400600)]
```

### 11. Removing a Punit-Specific Config
Remove the thread-specific config; those threads fall back to domain default.
```ini
[REMOVE OpenMP.thread(0-3)]
```

### 12. Removing All Thread Configs (Wildcard)
Remove all `OpenMP.thread(*)` configs without knowing individual sets.
```ini
[REMOVE OpenMP.thread(*)]
```

### 13. Automatic Mode Switching After Tracing
Trace 100 executions of each lexgion, then switch all domains to MONITORING.
```ini
[Lexgion.default]
    max_num_traces = 100
    tracing_rate = 1
    window_end_action = MONITORING
```

### 14. Per-Domain Auto Mode Switch
Trace 50 executions of a specific region, then set OpenMP to MONITORING and MPI to OFF.
```ini
[Lexgion(0x400500)]
    max_num_traces = 50
    window_end_action = OpenMP:MONITORING, MPI:OFF
```

### 15. Introspect-Analyze-Resume Workflow
Trace 100 executions, then introspect for 60 seconds while running an analysis script. The app resumes in TRACING mode after the script sends SIGUSR1 or the timeout expires. PInsight automatically runs `lttng rotate` before launching the script.
```ini
[Lexgion.default]
    max_num_traces = 100
    tracing_rate = 10
    window_end_action = INTROSPECT:60:analyze_traces.sh:TRACING
```

### 16. INTROSPECT via Environment Variable
Same as above, configured entirely via env var. The `PINSIGHT_TRACE_WINDOW`
variable mirrors `[Lexgion.default]`; its grammar is
`start:max:rate:window_timeout[:window_end_action_string]` (see "Environment Variables"
below). Here `window_timeout` is `0` (disabled):
```bash
PINSIGHT_TRACE_WINDOW=0:100:10:0:INTROSPECT:60:analyze_traces.sh:TRACING
```

### 17. Indefinite INTROSPECT (Interactive Debugging)
Introspect indefinitely with no script — only SIGUSR1 resumes the app.
```ini
[Lexgion.default]
    max_num_traces = 50
    window_end_action = INTROSPECT:-1:-
```

### 18. Fire-and-Forget INTROSPECT (No Pause)
Run the analysis script immediately without pausing the application. Useful for background analysis that doesn't need the app to stop.
```ini
[Lexgion.default]
    max_num_traces = 100
    window_end_action = INTROSPECT:0:analyze_traces.sh:TRACING
```

### 19. INTROSPECT with STANDBY Resume
After introspection, resume in STANDBY mode (near-zero overhead, recoverable).
```ini
[Lexgion.default]
    max_num_traces = 200
    window_end_action = INTROSPECT:60:analyze.sh:STANDBY
```

### 20. Time-Windowed Capture (`window_timeout`, standalone)
Trace for the first 30 wall-clock seconds, then drop to MONITORING — no count cap
needed. Useful to bound trace volume/overhead deterministically (e.g. on GPU runs
where activity records are not rate-limited).
```ini
[HIP.default]
    trace_mode = TRACING
[Lexgion.default]
    window_timeout = 30
    window_end_action = HIP:MONITORING
```

### 21. Count OR Timeout, with the `all` Policy
End the window when **all** regions this thread has seen have hit 50 traces — but
guarantee the window closes by 60 s regardless (backstop for the `all` never-fires
case). Whichever fires first wins.
```ini
[Lexgion.default]
    max_num_traces = 50
    window_end_trigger = all
    window_timeout = 60
    window_end_action = MONITORING
```

### 22. Cyclic INTROSPECT Bounded by `window_timeout`
Each TRACING window ends by 45 s (or earlier if 100 traces are reached), runs the
analysis script, and resumes to TRACING for the next window. The timeout re-arms
every cycle, so each window is bounded.
```ini
[Lexgion.default]
    max_num_traces = 100
    window_timeout = 45
    window_end_action = INTROSPECT:30:analyze.sh:TRACING
```

### 23. GPU Activity on One Rank/Node (`device_activity`)
Trace host-side HIP on every rank, but collect the GPU activity pool on only the node
leader — cutting the multi-rank activity-collection overhead while keeping GPU-exec data
for one GPU/node. (Set `on` for all GPUs, `off` for host-only.)
```ini
[HIP.default]
    trace_mode = TRACING
    HIP_kernel_launch = on
    device_activity   = leader_per_node
```

### 24. One-Rank-Per-Node Energy (`[Energy] measure`)
Measure node energy on exactly one rank/node (avoids multiple-counting the shared node
counters). Equivalent env: `PINSIGHT_MEASURE_ENERGY=LEADER_PER_NODE` (overrides the file).
```ini
[Energy]
    measure = leader_per_node
```

---

## Environment Variables

Configuration can also be supplied without a file:

| Variable | Purpose |
|----------|---------|
| `PINSIGHT_TRACE_CONFIG_FILE` | Path to a trace-config file (otherwise `./pinsight_trace_config.txt`). |
| `PINSIGHT_TRACE_<DOMAIN>` | Override one domain's mode, e.g. `PINSIGHT_TRACE_HIP=MONITORING`. Accepts `OFF`/`STANDBY`/`MONITORING`/`TRACING` (and `ON`/`1`/`TRUE` → TRACING). |
| `PINSIGHT_TRACE_WINDOW` | Configure the default tracing window (replaces `[Lexgion.default]` rate/window/mode-after knobs). |
| `PINSIGHT_MEASURE_ENERGY` | Energy node-policy: `ON`/`OFF`/`ANYONE_PER_NODE`/`LEADER_PER_NODE`. **Overrides** `[Energy] measure`. One-time at startup (per-run). |
| `PINSIGHT_NODE_LEADER` | Optional launcher-set hint (`1` on the chosen rank) that designates the node leader for `leader_per_node` policies; otherwise a local-rank env var / MPI is used. |

> **Removed:** `PINSIGHT_HIP_DISABLE_ACTIVITY` — replaced by `[HIP.default] device_activity = off` (config-only; `device_activity` also defaults to `off`).

### `PINSIGHT_TRACE_WINDOW` grammar
```
PINSIGHT_TRACE_WINDOW = start : max : rate : window_timeout [ : window_end_action_string ]
```
Read as: *the window starts at `start`, caps at `max` traces, is sampled at `rate`,
times out after `window_timeout` seconds, then performs `window_end_action_string`.*
- `window_timeout` is integer seconds; `0` = disabled.
- To set a mode-after with **no** timeout, put `0` in the 4th field:
  `0:50:1:0:HIP:MONITORING`. For a timeout with **no** count cap:
  `0:-1:1:30:HIP:MONITORING`.
- `window_end_action_string` accepts everything `window_end_action` does, including
  `INTROSPECT:pause:script[:resume_mode]`.
- Example — cap at 50 traces *or* 30 s, then HIP→MONITORING:
  `PINSIGHT_TRACE_WINDOW=0:50:1:30:HIP:MONITORING`.

> **⚠ Deprecation / breaking change.** This variable was formerly
> `PINSIGHT_TRACE_RATE` with grammar `start:max:rate[:window_end_action_string]` (no
> `window_timeout` field). `PINSIGHT_TRACE_RATE` is **still accepted as a
> deprecated alias** (same new grammar) and prints a one-line warning. Existing
> values that used a 4th field (e.g. `0:50:1:HIP:MONITORING`) must insert the new
> `window_timeout` field: `0:50:1:0:HIP:MONITORING`.
