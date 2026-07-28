# Streaming PInsight/LTTng traces to a remote analysis host

**Status:** design / experiment plan · 2026-06-29
**Goal:** relay a PInsight tracing session from the traced node (e.g. a Tuolumne
MI300A compute node running 4-rank AMG2023) to a *separate* computer for analysis
and visualization, via either (1) **rotation + relay** of completed trace chunks,
or (2) **live streaming**. Clients considered: `babeltrace2` (dump / Python
consumer) and **TraceCompass** (GUI, batch and live).

> **No PInsight code change is required for the core experiment.** PInsight emits
> LTTng-UST tracepoints; *where* the trace goes (local disk / relayd / live) is a
> property of how the **LTTng session** is created. Streaming is therefore mostly
> session configuration plus a relay topology. Optional integration hooks are in §8.

## 0. Verified toolchain (this environment, 2026-06-29)

- LTTng 2.13.14 (`~/local/bin/lttng`, `lttng-sessiond`, **`lttng-relayd`**).
- Babeltrace 2.0.6 with the **`source.ctf.lttng-live`** plugin (the live client).
- All current sessions use local output: `lttng create <name> --output=<dir>`
  (e.g. [test/rocm/window_timeout_test.sh](../test/rocm/window_timeout_test.sh),
  [eva/AMG2023-tuolumne/overhead_experiment.sh](../eva/AMG2023-tuolumne/overhead_experiment.sh)).
- [scripts/trace.sh](../scripts/trace.sh) is the session wrapper (has a commented
  `--snapshot` path already).

## 1. LTTng remote primitives

Two concerns are **orthogonal**: *transport* (where bytes go) and *readability*
(when they can be read). The two options are points in that space.

| Primitive | Effect | Command |
|---|---|---|
| **Network output** | stream trace data over TCP to a `lttng-relayd` on another host | `lttng create S --set-url=net://REMOTE` |
| **Live mode** | make a networked session *incrementally readable* by a live viewer | `lttng create S --live 1000000 --set-url=net://REMOTE` |
| **Rotation / chunks** | finalize complete, immutable CTF "chunks" you can move/analyze | `lttng enable-rotation --timer 10s` · `lttng rotate S` |
| **Snapshot** (flight recorder) | ring buffer only; dump on demand | `lttng create S --snapshot` → `lttng snapshot record` |

`lttng-relayd` ports: **5342** control, **5343** data, **5344** live.

**Mutually-exclusive session modes (important):** a session is *normal*
(rotatable), **or** *snapshot*, **or** *live*. You cannot rotate a live or
snapshot session. So Option 1 (rotation) and Option 2 (live) are **different
session types** — pick per run.

**Overhead note:** streaming does **not** add application-thread cost. UST writes
to per-CPU ring buffers; the consumer / relayd connection drains them
asynchronously, exactly like local disk (consistent with PInsight's async design).
The real risk is **backpressure**: if the drain (network) can't keep up, buffers
fill and events are *discarded* (discard mode) or the tracer *blocks*
(overwrite/blocking mode). See §7 (data volume).

## 2. The crux: HPC network topology (decides feasibility)

This is the dominant constraint on Tuolumne and the reason "just live-stream to my
laptop" is non-trivial:

- **MI300A compute nodes** (`tuolumne[1xxx]`) are on the cluster's internal fabric.
  They generally **cannot** open TCP to an arbitrary external host, and external
  hosts **cannot** reach them (egress/ingress firewalls).
- The **login node** `tuolumne1` *is* reachable from compute nodes (internal) and
  from your workstation (via SSH).

Realistic relay placements:

- **(A) relayd on the login node.** Compute job streams to the login-node relayd
  over the internal network; your workstation reads it through an **SSH tunnel**.
  → most viable for *live*.
- **(B) relayd on your workstation** via a *reverse* SSH tunnel from the login
  node — more moving parts, usually unnecessary.
- **(C) no relayd — shared filesystem.** The job writes chunks to Lustre/GPFS
  (visible on the login node); you `rsync`/read chunks from there. → lowest risk,
  no open ports; the natural home for Option 1.

**Key engineering insight:** Option 1 (chunks) can ride entirely on the **shared
FS + SSH/scp**, sidestepping relayd reachability. Option 2 (live) needs relayd
reachable from compute nodes, which on a locked-down system means
**relayd-on-login-node + SSH tunnel** (topology A). The single biggest unknown to
validate is **compute → login-node relayd reachability**.

## 3. Option 1 — Rotation + relay (chunk store-and-forward)

Near-line: analyze completed chunks shortly after they close.

**Mechanism**
1. Create a normal session and enable rotation:
   ```bash
   lttng create amg --output=$SCRATCH/amg-chunks      # shared FS
   lttng enable-event -u 'roctracer_pinsight_lttng_ust:*' \
                      -u 'pinsight_enter_exit_lttng_ust:*' # + ompt_*, pmpi_* as built
   lttng enable-rotation --timer 10s                  # or --size 256M; or manual lttng rotate
   lttng start
   ```
   PInsight already calls `lttng rotate` during INTROSPECT — that rotation is a
   natural chunk boundary (§8).
2. Each rotation finalizes an **immutable** chunk directory under the output path.
   A watcher on the login node (inotify or poll) ships **completed** chunks:
   ```bash
   # simplistic: rsync closed chunks to the analysis host
   while inotifywait -e moved_to,create "$SCRATCH/amg-chunks"; do
     rsync -a --ignore-existing "$SCRATCH/amg-chunks/" analysis:/data/amg/
   done
   ```
   (Transfer only *closed* chunks; the active chunk is still being written.)
3. Analysis host runs `babeltrace2 <chunk>` (batch) or imports into TraceCompass.

**Variant 1.5 — snapshot / flight-recorder.** `lttng create --snapshot` + an
INTROSPECT-triggered `lttng snapshot record` captures only the window around an
event, then relays that snapshot. Bounds data volume and fits PInsight's in-situ
introspection model.

**Pros:** robust over firewalled/flaky links (file transfer only); chunks are
complete → no partial reads; disk absorbs bursts (decouples app from network
bandwidth — matters for AMG's high GPU-activity rate); reuses existing
`lttng rotate`; works directly off the shared FS with **no relayd**.
**Cons:** latency = rotation period (near-line, not live); needs a transfer/dedup
watcher; node-local/shared storage for chunks.

## 4. Option 2 — Live streaming

Real-time: views update as events are produced.

**Mechanism (topology A — relayd on login node)**
1. On the login node:
   ```bash
   lttng-relayd -o ~/relay-traces -d         # daemonize; listens 5342/5343/5344
   # (ports overridable: -C tcp://0.0.0.0:5342 -D ...:5343 -L ...:5344)
   ```
2. On the compute node (inside the flux job), create a **live** session pointing
   at the login node:
   ```bash
   lttng create amg-live --live 1000000 --set-url=net://tuolumne1   # 1s live timer
   lttng enable-event -u 'roctracer_pinsight_lttng_ust:*' -u 'pinsight_enter_exit_lttng_ust:*'
   lttng start
   ./amg2023 ...    # under LD_PRELOAD=libpinsight.so
   ```
   Trace streams compute → login-node relayd over TCP.
3. From your workstation, tunnel to the relayd live port and attach a viewer:
   ```bash
   ssh -L 5344:localhost:5344 tuolumne1
   # list live sessions:
   babeltrace2 --input-format=lttng-live net://localhost:5344
   # read one live:
   babeltrace2 net://localhost:5344/host/<compute-node>/amg-live
   ```
   …or point **TraceCompass** at the same `lttng-live://localhost:5344/...`
   endpoint (§5).

**The `--live USEC` timer** controls how often UST flushes sub-buffers so a viewer
sees data even at low event rates; smaller = lower latency, slightly more overhead.

**Pros:** real-time visualization; no manual transfer; aligns with PInsight's
in-situ philosophy; can drive live dashboards / remote INTROSPECT decisions.
**Cons:** needs relayd reachable from compute (the topology problem); continuous
TCP on the drain path → backpressure risk under high event rates; live + rotation
not combinable; multi-stream handling.

## 5. Clients / processing on the remote host

`babeltrace2` and **TraceCompass** are **two independent clients of the same
LTTng-live protocol** — TraceCompass has its *own* Java CTF reader and live-protocol
client; it does **not** shell out to babeltrace2. Either, both, or neither.

```
                          ┌─ babeltrace2 (source.ctf.lttng-live) → text / Python
 lttng-relayd :5344  ──── ┤
   (live protocol)        └─ TraceCompass (own Java live client)  → GUI views
```

| Client | Effort | What you get |
|---|---|---|
| **babeltrace2 live dump** | trivial | text event stream; best to validate the pipeline; pipe to Python |
| **babeltrace2 → Python consumer** | low | live dashboards / feed INTROSPECT; reuses the `babeltrace2`-based `analyze_amg.py` pattern |
| **TraceCompass — generic CTF** | low | event table + generic views over PInsight CTF, **batch or live** |
| **TraceCompass — custom analysis** | medium | a real **multi-domain timeline** (per-rank/thread rows, kernel/MPI/OpenMP spans) — see §6 |

**TraceCompass live, concretely:** the session must be created with `--live`; point
TraceCompass at the relayd **live port (5344)** via an `lttng-live://host:5344/...`
connection. Its event table, Control Flow / Resources views, and state-system
analyses **extend incrementally** as data arrives (the state system is built
incrementally). Caveats: updates are stepwise (paced by the `--live` timer + the
GUI's polling), it can lag under high event rates (the headless babeltrace2 path is
more robust at high throughput), and live + rotation is a weak combination. (Exact
menu path varies by TraceCompass version; the capability and the `:5344` endpoint
are the stable facts.)

## 6. TraceCompass analysis for PInsight semantics — reuse the `peam` XML

Independent of live-ness. Out of the box TraceCompass shows PInsight events in the
**event table** (live or batch), but a meaningful **timeline** (rows per
thread/device, spans for kernels / MPI / OpenMP / sync) needs a **custom analysis**
— a TraceCompass **XML data-driven analysis** (no Java plugin).

**This already largely exists** in the **`peam` repo**
(github.com/passlab/peam, cloned at `peam/src/tracecompass/`):

- **`pinsight_analysis.xml`** — a full **state provider + time-graph view** bound
  to the generic LTTng-UST trace type:
  - **OpenMP (OMPT): complete** — thread / parallel / implicit_task / work / masked
    + explicit/implicit barrier & join sync + wait states, over a location
    hierarchy (Processor → OS Thread → OMP Global Thread → OMP Team/Thread, plus
    Parallel Region / Region-Instance). Includes a 64-processor + OMP-state color
    map and OS-Thread / OMP-Team / Parallel-Region time-graph views.
  - **CUDA (CUPTI): complete** — `cudaMemcpy` (direction from `cudaMemcpyKind`),
    `cudaKernelLaunch`, over CUDA Device / Device-Kernel locations + a CUDA view.
  - **MPI: empty stub.**
- **`pinsight_omp_pattern_analysis.xml`** — an FSM **pattern/segment** analysis
  emitting per-`parallel_region` segments (stored `team_size`,
  `parallel_record_id`) → statistics/density/duration views.

**Crucially, these are event-driven and trace-type-generic, so they apply
unchanged to batch CTF, relayed chunks, *and* live sessions** — and the state
system is built incrementally, so they update in live mode too. "Live
visualization of PInsight semantics" = live protocol (free) **+** this XML.

### Gap to close for the AMG2023-tuolumne target (MPI + HIP on MI300A)

The `peam` XML predates the HIP domain, so for the AMG eval it needs:

1. **HIP block — near-direct copy of the CUDA block** (highest value). Handlers for
   `roctracer_pinsight_lttng_ust:` `hipKernelLaunch_begin/_end` (carries grid/block
   dims), `hipMemcpy_begin/_end` and `hipMemcpyAsync_begin/_end`, `hipDeviceSync` /
   `hipStreamSync` `_begin/_end`, `hipMalloc`/`hipFree`, and the GPU **activity**
   records `hipKernelActivity` / `hipMemcpyActivity`. **Schema (verified against
   [src/roctracer_lttng_ust_tracepoint.h](../src/roctracer_lttng_ust_tracepoint.h)):**
   - memcpy direction is a **`hipMemcpyKind` enum field** — *exactly* the CUDA
     `cudaMemcpyKind` pattern — so the HIP memcpy handler is the CUDA nested-`if`
     handler with the event/field/enum-label names swapped (e.g.
     `hipMemcpyHostToDevice`).
   - common fields on every host event: `mpirank`, `global_thread_num`,
     `omp_team_num/omp_thread_num`, **`devId`**, `correlation_id`, **`hip_func`**
     (kernel name), `hip_codeptr`. → HIP Device location keyed on `devId`, kernel
     location on `hip_func`.
   - **activity records** carry `devId`, `correlation_id`, and their own
     `begin_ns`/`end_ns` (GPU-side interval) + `kernelName`/`queueId`. These are GPU
     execution spans that correlate to the host launch via `correlation_id`; render
     them on a per-`devId` "GPU" row (an FSM/pattern keyed on `begin_ns`/`end_ns`
     is the clean way, since the record arrives at flush time, not at span start).
2. **MPI handlers + `mpirank` grouping.** Fill the MPI stub (`pmpi_pinsight_lttng_ust:*`)
   and add an `mpirank` top-level location so the 4-rank AMG run shows one
   sub-tree per rank (PInsight already records `mpirank`; activity `devId` maps to
   rank as `rank+4` per the AMG analysis).
3. *(optional)* **Python** (`pysysmon_pinsight_lttng_ust:*`) for cross-domain runs.

**Status (2026-06-29):** items 1–2 are **drafted and merged into the single
combined state provider** `peam/src/tracecompass/pinsight_analysis.xml` (one
`stateProvider` + one `timeGraphView` for the unified cross-domain timeline — see
the decision below). Added: a HIP device timeline (kernel launch, sync/async memcpy
by `hipMemcpyKind`, malloc/free, device/stream sync) mirroring the CUDA block, and
an MPI **per-rank → per-thread** timeline keyed on a new `MPI Rank` grouping (P2P /
collective / wait / init categories), plus HIP/MPI color maps and view entries.
XML-validated well-formed; coverage now OMPT(22) + CUDA(4) + HIP(14) + MPI(36)
handlers, 7 time-graph entry trees. Not yet opened against a real trace in
TraceCompass. The GPU **activity** records (`hipKernelActivity`/`hipMemcpyActivity`)
are intentionally excluded — they need a segment/Java analysis (a comment at the
HIP section explains why).

**Organization decision (2026-06-29):** keep **one combined state provider +
time-graph view** for the timeline (cross-domain correlation on a shared time axis
requires a single `stateProvider`/view — TraceCompass XML cannot compose separate
providers into one view), and keep **pattern/statistics analyses per-domain**
(`pinsight_omp_pattern_analysis.xml`, future `pinsight_hip_pattern_analysis.xml`,
…), since those are independent FSM/segment analyses with their own views.

This XML work is the natural Phase 3 (§9); Phases 0–2 (babeltrace2 dump + pipeline)
don't need it.

## 7. Data volume & backpressure

AMG2023 (4 ranks/4 APUs, n=60³) emits on the order of ~26k GPU activity records per
run plus host callbacks (see
[code-memory/amg2023_eval_overhead.md](code-memory/amg2023_eval_overhead.md)).
Implications:

- **Option 1** decouples app from network: bursts hit local/shared disk first;
  transfer is offline. Safe default for high rates.
- **Option 2** puts the network on the drain path. Size ring buffers generously
  (`--num-subbuf` / `--subbuf-size` on `lttng enable-channel`), prefer **discard**
  over blocking to protect the app, and watch relayd for dropped packets. Use
  PInsight's own **rate control** (`max_num_traces`, `window_end_trigger`) and
  `window_timeout` / mode switching to cap volume — these compose naturally with
  streaming and are the right knob if live can't keep up.

## 8. PInsight integration (optional, after validation)

- **`scripts/trace.sh`**: add a `--relay HOST` / `--live` mode that starts/locates
  relayd and sets `--set-url` / `--live` (mirrors the existing local/`--snapshot`
  paths).
- **INTROSPECT hook**: PInsight already runs `lttng rotate` during INTROSPECT
  (control thread). For Option 1, that rotation *is* the relay chunk boundary; for
  Variant 1.5, trigger `lttng snapshot record` there instead. This makes the relay
  event-driven by the application's own introspection.
- No changes to the tracepoints, clock calibration, or event schema are needed —
  the CTF carries its own clock, so the remote reader interprets timestamps with no
  cross-machine clock sync.

## 9. Phased experiment plan (de-risk the network first)

- **Phase 0 — loopback, no GPU.** On one host (this dev box or the login node),
  trace an OpenMP app (`test/mode_window/mode_test` + `build_omp/libpinsight.so`),
  run `lttng-relayd` locally, create a `--live` session to `net://localhost`, and
  read it with `babeltrace2 net://localhost:5344/...`. Proves the live pipeline
  with zero network risk. Repeat for rotation → chunk → `babeltrace2`.
- **Phase 1 — two hosts over LAN/SSH.** relayd on one (workstation or login node),
  client on the other via SSH tunnel. Validates reachability, the tunnel, and
  TraceCompass live.
- **Phase 2 — Tuolumne.** relayd on the login node; AMG job on a compute node
  streams to it; read live from the login node and/or tunnel to the workstation.
  **This tests compute → login reachability** (the make-or-break unknown). In
  parallel, validate Option 1 via shared-FS chunks (should "just work").
- **Phase 3 — TraceCompass XML analysis** (§6) for the real multi-domain timeline.

## 10. Comparison & recommendation

| Dimension | Option 1: rotate + relay | Option 2: live |
|---|---|---|
| Latency | near-line (rotation period) | real-time |
| HPC firewall risk | **low** (file transfer / shared FS) | medium–high (needs relayd reachable) |
| Backpressure risk | low (disk absorbs bursts) | higher (network on drain path) |
| Setup complexity | low–medium (watcher + transfer) | medium (relayd + tunnel + viewer) |
| PInsight fit | reuses `lttng rotate` / snapshot | fits in-situ; can drive remote INTROSPECT |
| Best client | `babeltrace2` batch, TraceCompass batch | `babeltrace2` live, TraceCompass live |

**Recommendation:** pursue **both, leading with Option 1** (chunk / shared-FS) as
the robust baseline that is nearly certain to work on Tuolumne and reuses existing
plumbing, while developing **Option 2 (live)** as a Phase-0/1 prototype on friendly
networks and attempting it on Tuolumne only via **relayd-on-login + SSH tunnel**.
For clients, start with **babeltrace2 live dump** (fastest signal), add
**TraceCompass generic CTF**, then invest in the **XML analysis** for real
visualization. Validate **compute → login relayd reachability** early — it gates
the entire live path.

## 11. Related
- **`peam` repo** (github.com/passlab/peam, cloned at `peam/src/tracecompass/`) —
  existing TraceCompass XML: `pinsight_analysis.xml` (OMP + CUDA state provider /
  time-graph view; MPI stub; no HIP) and `pinsight_omp_pattern_analysis.xml`
  (parallel-region segment statistics). The basis for §6.
- [scripts/trace.sh](../scripts/trace.sh) — session wrapper to extend
- [pinsight_feature_summary.md](pinsight_feature_summary.md),
  [signal_handler_reload_config.md](signal_handler_reload_config.md) — INTROSPECT /
  control-thread context for the rotation/snapshot hooks
- [code-memory/amg2023_eval_overhead.md](code-memory/amg2023_eval_overhead.md) —
  event volumes that drive the backpressure analysis

## 12. Experiment log & current status (2026-06-29 / 30)

Plan: **tux439.llnl.gov** = the `babeltrace2`-based client; **a laptop** = the
TraceCompass client. Trace source = `tuolumne2151.llnl.gov` (a Tuolumne
login/service node, in the scheduler-excluded `2149-2152` range). GPU-free OpenMP
app (`test/mode_window/mode_test` + `build_omp/libpinsight.so`) used as the
producer for pipeline validation.

### 12.1 Network reality on Tuolumne (measured)
- **Tuolumne → external is firewalled.** Outbound TCP from `tuolumne2151` to
  `tux439` on ports 22 / 5342 / 5344 / 443 / 80 **all time out**; only ICMP (ping)
  passes (1.8 ms). So a Tuolumne node **cannot initiate** a relay/live connection
  outward, and external hosts cannot reach it inbound.
- **SSH from the clients INTO Tuolumne works** (confirmed: tux439 → tuolumne). SSH
  is the firewall's one sanctioned hole.
- **Corrected conclusion (supersedes the earlier "tux439 = shared-FS only"):**
  **live streaming works to BOTH tux439 and the laptop — via an `ssh -L` tunnel**,
  because the tunnel rides the *inbound* SSH (which works), not the *outbound* TCP
  (which is blocked). The earlier shared-FS-only call was based on the blocked
  outbound direction.
- **Shared filesystem** also available to tux439: the CZ global FS (`/g/g19`,
  `/usr/workspace/yan10`) is mounted on Tuolumne and (being CZ-global) on tux439
  too. The laptop is *not* on the shared FS → laptop uses tunnel (live) or
  `scp`/`rsync` (batch).
- Tools (LTTng 2.13.14, babeltrace2 2.0.6 + `source.ctf.lttng-live`,
  `lttng-relayd`) are in `~/local/bin` = shared `/g/g19/yan10/local/bin`.

### 12.2 Validated so far (source side, on tuolumne2151)
- **Option 1 (shared-FS batch): DONE.** A real PInsight CTF lives at
  **`/g/g19/yan10/pinsight_streaming/run1`** (1636 events: OMPT parallel/task/sync
  + `enter_pinsight`; fields `global_thread_num`, `omp_team_num`,
  `parallel_codeptr`, … as the XML analysis expects). Read back OK with
  `babeltrace2` locally. **Pending:** tux439 reads it
  (`babeltrace2 /g/g19/yan10/pinsight_streaming/run1` → expect 1636).
- **Option 2 (live): pipeline PROVEN on-node (loopback).** `lttng-relayd` +
  `lttng create --live 1000000 --set-url=net://localhost` + `babeltrace2` live read
  → **7225 events read live in 6 s** while the app produced. A persistent
  background producer also ran a ~35 min live window with relayd bound on all
  interfaces (`0.0.0.0:5342/5343/5344`), session discoverable as
  `net://localhost:5344/host/tuolumne2151/stream-live`; it self-cleaned at the end
  of its window. **Pending:** a client connects through the tunnel (not yet done
  from tux439/laptop).

### 12.3 Reproduction recipes
**Source (on the Tuolumne node) — start a live session:**
```bash
export PATH=$HOME/local/bin:$PATH LD_LIBRARY_PATH=$HOME/local/lib:$LD_LIBRARY_PATH
lttng-relayd -o ~/relay-traces -L tcp://0.0.0.0:5344 -d      # 0.0.0.0 live port for tunneling
lttng create stream-live --live 1000000 --set-url=net://localhost
lttng enable-event -u 'ompt_pinsight_lttng_ust:*'            # NOTE: one -u per command
lttng enable-event -u 'pinsight_enter_exit_lttng_ust:*'
lttng start
LD_PRELOAD=/lib64/libomp.so:$PWD/build_omp/libpinsight.so \
  PINSIGHT_TRACE_CONFIG_FILE=<cfg with [OpenMP.default] trace_mode = TRACING> \
  OMP_NUM_THREADS=4 ./mode_test 1000 50 10            # keep producing
```
**Client (tux439 or laptop) — live via SSH tunnel:**
```bash
ssh -N -L 5344:localhost:5344 yan10@tuolumne2151.llnl.gov   # direct to the relayd node
#   (or, landing on another login:  ssh -N -L 5344:tuolumne2151:5344 yan10@<login>  — 5344 is 0.0.0.0)
babeltrace2 --input-format=lttng-live net://localhost:5344                       # list
babeltrace2 net://localhost:5344/host/tuolumne2151/stream-live                   # read (Ctrl-C to stop)
# TraceCompass: LTTng-Live connection → localhost:5344 ; load peam/.../pinsight_analysis.xml
```
**Client — batch (tux439 shared-FS):** `babeltrace2 /g/g19/yan10/pinsight_streaming/run1`

### 12.4 Gotchas hit (for reproducibility)
- `pkill -f lttng-sessiond` matches *your own shell* (the cmdline contains the
  string) → kills the caller. Use `pkill -x lttng-sessiond` / `-x lttng-relayd`.
- `lttng enable-event -u A -u B` in one call **fails** ("Unknown argument") → one
  `-u` spec per `enable-event` command.
- A background producer launched in one shell does **not** survive into the next
  command/turn (orphaned). Run producer + reader in the **same** shell, or as a
  persistent background job; the live reader needs the producer **alive**.
- `lttng-relayd` binds the **live port (5344) on 127.0.0.1 by default**. For
  tunneling via a *different* login node, start it with `-L tcp://0.0.0.0:5344`.
- The first run on a node may have a stale/wedged `lttng-sessiond` (lock file
  `~/.lttng/lttng-sessiond.lck`); reset with `pkill -x lttng-sessiond` + remove the
  lock, then re-`lttng create`. Note the lock file lives in *shared* home, so a
  lock left by a session on one login node (e.g. 2151) blocks `lttng create` on
  another (e.g. 2152) — same reset applies.
- The laptop cannot SSH to a *specific* login node; `tuolumne.llnl.gov`
  load-balances across 2149–2152. Tunnel with a **remote-side target** instead:
  `ssh -S none -N -L 5344:tuolumne2152:5344 yan10@tuolumne.llnl.gov` — whichever
  node you land on forwards to 2152 over the internal network (login→login TCP to
  5344 verified open; requires relayd's `-L tcp://0.0.0.0:5344`). `-S none`
  bypasses a ControlMaster mux that refuses added forwards; if the local port is
  busy (stale ssh), pick another local port — only the first number changes.
- TraceCompass/Eclipse SSH (`ssh://…` fetch, Control view) fails against
  Tuolumne's hardened sshd with `Algorithm negotiation fail` — client-side fix in
  **§13**.

### 12.5 Status by client × path
| Client | Live (Option 2, `ssh -L`) | File-based (Option 1) |
|---|---|---|
| **tux439** (babeltrace2) | ✅ source ready; client connect **pending** | ✅ trace at `…/pinsight_streaming/run1`; tux439 read **pending** |
| **laptop** (TraceCompass) | ✅ same tunnel → `localhost:5344`; **pending** | `scp`/`rsync` the CTF, then open (not started) |

### 12.6 Next steps (resume here)
1. **tux439 batch** (no coordination): `babeltrace2 /g/g19/yan10/pinsight_streaming/run1` → confirm 1636.
2. **tux439 live**: relaunch the background producer **on demand**, then connect via
   the §12.3 tunnel recipe; confirm `babeltrace2` lttng-live event count.
3. **laptop + TraceCompass**: same tunnel → TraceCompass LTTng-Live to
   `localhost:5344`; load the merged `peam/src/tracecompass/pinsight_analysis.xml`
   (OMP+CUDA+HIP+MPI) and verify the unified timeline renders live.
4. **HIP/AMG (real workload)**: relayd on the login node; flux job on a compute
   node streams to it over the internal network (relayd data ports are `0.0.0.0`);
   client tunnels to the login node's 5344. Needs the flux bank.
5. Optionally add **rotation** (`lttng enable-rotation --timer 10s`) for the
   near-line chunk variant of Option 1.

The producer helper used here: a small loop of `mode_test` runs guarded by an EXIT
trap that tears down relayd + session (kept in the session scratchpad, not the
repo). Re-create from §12.3 if needed. A 3-hour self-cleaning variant lives at
`/g/g19/yan10/pinsight_streaming/live-demo/producer.sh`.

## 13. Fixing TraceCompass/Eclipse SSH against a hardened sshd (solved 2026-07-15/16)

Both TraceCompass remote features — **Fetch Remote Traces** (`ssh://user@host`,
SFTP batch copy) and the **LTTng Control view** (needed to open *live* sessions)
— ride Eclipse's JSch SSH stack (PTP `org.eclipse.remote.jsch.core` → library
bundle `com.jcraft.jsch`). Against Tuolumne's sshd it fails with
`Algorithm negotiation fail` **before authentication**. Tuolumne's sshd offers a
modern-only set (measured via `ssh -vv`, "peer server KEXINIT proposal"): KEX
**ecdh-sha2-nistp256/384/521 only**, host keys **rsa-sha2-512/256 +
ecdsa-sha2-nistp256** (no SHA-1 `ssh-rsa`), MACs **hmac-sha2-\*** only.

**Root cause (confirmed 2026-07-16):** an Eclipse *preference* overrides JSch's
KEX list with a DH-only legacy default → empty intersection with the
ECDH-only server. **The preference edit (§13.1) alone is the minimal sufficient
fix** — verified on a fresh Eclipse install (`~/.p2` wiped) with the stock JSch
0.1.55 bundle: the Tuolumne connection succeeds. The library swap (§13.2) is
*optional but recommended* hardening, and was essential for *diagnosing* this
(stock JSch reports only the bare error). No marketplace plugin addresses either
part — the SSH library is a platform bundle, not an installable feature.
(Verified on Eclipse 2026 releases: still ships JSch 0.1.55, from 2018, for the
PTP path.)

### 13.1 The fix — Eclipse preferences force a DH-only KEX list

Eclipse's JSch integration applies an algorithm list from workspace preferences,
whose *default predates ECDH* — so the client proposes only
`diffie-hellman-group*` while the server offers only `ecdh-*`: empty
intersection. (This is why even stock JSch 0.1.55 fails, despite implementing
ECDH since 0.1.50.) The SSH2 preference UI (General → Network Connections →
SSH2) **cannot express ECDH** (its choice list is the same legacy default), so
edit the file directly, **with Eclipse closed** (it rewrites prefs on exit):

`<workspace>/.metadata/.plugins/org.eclipse.core.runtime/.settings/org.eclipse.jsch.core.prefs`
(if the KEX keys are absent, make any change on the SSH2 page + Apply once to
materialize them):

```
CVSSSH2PreferencePage.PREF_KEX_METHODS=ecdh-sha2-nistp256,ecdh-sha2-nistp384,ecdh-sha2-nistp521,diffie-hellman-group-exchange-sha256,diffie-hellman-group14-sha1,diffie-hellman-group-exchange-sha1,diffie-hellman-group1-sha1
CVSSSH2PreferencePage.PREF_KEX_METHODS_ORDER=<same value>
CVSSSH2PreferencePage.PREF_MAC_METHODS=hmac-sha2-256,hmac-sha2-512,hmac-sha1,hmac-md5,hmac-sha1-96,hmac-md5-96
CVSSSH2PreferencePage.PREF_MAC_METHODS_ORDER=<same value>
```

Change **both** the key and its `_ORDER` twin. There is no host-key preference
key, so the library's `server_host_key` defaults apply untouched — stock 0.1.55
negotiates `ecdsa-sha2-nistp256`, which the server (still) offers. Restart and
retry — **confirmed working with both stock 0.1.55 and the fork** (laptop →
`ssh://yan10@tuolumne.llnl.gov:22` fetch connection succeeds). Optional
belt-and-suspenders: JVM overrides in `eclipse.ini` after `-vmargs`
(`-Djsch.kex=…`, `-Djsch.server_host_key=…`, `-Djsch.mac=…`) — inert if the
preference layer overrides them.

### 13.2 Optional but recommended — swap in the maintained JSch fork

Not required for Tuolumne today, but recommended, and essential for *diagnosis*:
the fork reports *which* algorithm category failed and both proposals
(`algorithmName="kex" jschProposal=… serverProposal=…` — 0.1.55 gives only the
bare `Algorithm negotiation fail`, which is undebuggable). It also removes two
latent breakpoints of 0.1.55, which is SHA-1-RSA-only: it cannot verify
`rsa-sha2-*` host-key signatures (dead the day the server drops its ECDSA host
key) and cannot do RSA public-key auth against modern sshds (SHA-1 signatures
rejected). It adds the strict-KEX/Terrapin mitigation the server advertises.

Replace `com.jcraft.jsch` 0.1.55 with the mwiede fork (drop-in: same
`com.jcraft.jsch` packages/API; 2.x versioning only signals a Java 11+ baseline):

1. Download the latest from Maven Central (`com.github.mwiede:jsch`; 2.28.4 used
   here). Use `curl -fLO …` — `-f` fails loudly on a 404 instead of saving an
   HTML error page named `.jar`. Check the real version first:
   `curl -s https://repo1.maven.org/maven2/com/github/mwiede/jsch/maven-metadata.xml | grep -E '<latest>'`.
2. **Masquerade the jar as the old bundle** (OSGi `bundles.info` pins bundle
   symbolic name + version): unzip, edit `META-INF/MANIFEST.MF` →
   `Bundle-SymbolicName: com.jcraft.jsch`,
   `Bundle-Version: 0.1.55.v20230916-1400` (match the installed version exactly),
   and the `Export-Package: com.jcraft.jsch;version="…"` to the same version.
   Leave `Specification-/Implementation-Version` and the `resolution:=optional`
   imports (BouncyCastle/JNA/slf4j) untouched — OSGi ignores the former, and the
   needed algorithms run on plain JCE. (`Implementation-Version` is then also the
   only reliable way to tell fork from stock — `Bundle-Version` lies by design.)
   Preserve manifest 72-char wrapping (continuation lines start with one space).
   Re-zip (`zip -qr new.jar .` from inside the extracted dir).
3. Copy over the original **keeping the original filename**
   (`com.jcraft.jsch_0.1.55.v20230916-1400.jar`). Location: find it with
   `find /Applications ~/eclipse ~/.p2 -name "com.jcraft.jsch_*.jar"` — either
   inside the app (`Eclipse.app/Contents/Eclipse/plugins/`, Finder: "Show Package
   Contents") or, for Eclipse-Installer installs, the shared pool
   `~/.p2/pool/plugins/` (shared by all Oomph-managed installs — a swap there
   affects, and survives reinstalls into, the same pool). Keep a backup; if
   lost, the original is `com.jcraft:jsch:0.1.55` on Maven Central (JCraft's
   jar already carries the OSGi manifest — patch only the version qualifier).
4. Restart with `-clean` (`open Eclipse.app --args -clean`). macOS: if the app
   won't launch after modification (broken code signature),
   `xattr -dr com.apple.quarantine Eclipse.app` or
   `codesign --force --deep --sign - Eclipse.app`.

### 13.3 Second blocker after SSH negotiates — stray trap output corrupts remote XML

Even after §13.1, opening the **LTTng Control view** connection failed with
*"Error retrieving node configuration"* /
`SAXParseException; Content is not allowed in trailing section`. This means: a
well-formed XML document, then **extra text appended after it** — the signature
of shell startup noise polluting a remote command's stdout.

Root cause: `~/.profile.linux` (LLNL's per-user copy of a CFEngine skel file) had
an **unguarded** `trap "echo 'logout'" 0` at the very end — fires on *every*
shell exit, unlike the `tset`/`stty` block earlier in the same file, which is
correctly wrapped in `if [ "$ENVIRONMENT" = INTERACTIVE ]`. When Eclipse's remote
connection runs a command via a login shell, `.profile`/`.profile.linux` gets
sourced; the command's real (XML) output is emitted, the shell exits, and the
trap appends a trailing `logout` line — landing exactly where TraceCompass's
parser chokes.

Fix (safe: this is the user's own file, mode 600, not a shared system file):
```sh
# before (unconditional, in ~/.profile.linux):
	trap "echo 'logout'" 0
# after:
if [ "$ENVIRONMENT" = INTERACTIVE ]; then
	trap "echo 'logout'" 0
fi
```
`.profile` sets `ENVIRONMENT=BATCH` for any shell whose `$-` lacks `i` — exactly
what a remote SSH-exec'd command is — so the guard suppresses the trap there
while preserving it for real interactive logins. **Confirmed working** after this
edit: Control view connects cleanly. If the same error recurs with a different
column number, the guard didn't help and the noise source is elsewhere (check
`/etc/profile.d/*.sh` or an MOTD next); if it persists at the *same* column, this
fix didn't take effect (shell not re-read, or a different `.profile*` copy is in
play).

### 13.4 A second stale bundle — `org.apache.commons.compress` 1.4.1 (2013) — CONFIRMED WORKING 2026-07-16

After 13.1–13.3, Fetch Remote Traces on a *snapshot* trace still failed, this
time with `NoSuchMethodError` on
`TarArchiveInputStream.getNextEntry()` deep in `ArchiveUtil.isArchiveFile()`
(the fetch wizard defensively probes whether a remote path might be a tar
archive before copying it). Root cause: the installed
`org.apache.commons.compress` bundle is **1.4.1.v201301140946 — from 2013** —
and predates the generified `ArchiveInputStream` API (added in commons-compress
1.21, 2021) that this method signature requires. Same masquerade fix as 13.2:

1. Download latest from Maven Central (`org.apache.commons:commons-compress`,
   1.28.0 used here).
2. Edit `META-INF/MANIFEST.MF`: `Bundle-SymbolicName: org.apache.commons.compress`
   (**note:** the Maven jar's own symbolic name is
   `org.apache.commons.commons-compress` — must be trimmed, easy to miss),
   `Bundle-Version: 1.4.1.v201301140946`, and **every** `version="1.28.0"` in the
   (long) `Export-Package` line set to the same old version string.
3. **Rebuild with the real `jar` tool, not `zip`:**
   ```bash
   jar cfm out.jar META-INF/MANIFEST.MF -C <extracted-dir> .
   ```
   This matters more than it sounds: a naive `zip -qr` re-bundle, or worse, an
   in-place single-file update into an already-built jar, can physically
   relocate `META-INF/MANIFEST.MF` deep into the archive (observed: entry 638 of
   645). Several jar/OSGi loaders read a jar as a sequential stream and require
   the manifest to be the first (or one of the first) entries — a buried
   manifest makes the bundle **silently vanish from Eclipse's Plug-ins list
   entirely** (worse than the 13.2 symptom: not wrong content, no bundle at
   all). Verify before installing: `unzip -l out.jar | head -5` — the manifest
   must be entry #1 or #2.
4. Swap into the same location as 13.2 (`~/.p2/pool/plugins/` for Oomph-managed
   installs, or `Eclipse.app/Contents/Eclipse/plugins/`).

**Two editing gotchas hit in practice:**
- The original Bnd-generated manifest used **CRLF** line endings; a line-rewrap
  script that only `chomp`s `\n` (Perl default) leaves `\r` attached, and mixes
  newly-inserted `\n`-only breaks with original `\r\n` ones. Simplest fix:
  normalize the whole file to plain `\n` first (`perl -i -pe 's/\r$//'`), then
  rewrap against a clean 72-byte limit.
- **The 72-byte MANIFEST.MF line limit is 72 *content* bytes — the line
  terminator is separate/additional** (confirmed against the actual JDK
  `java.util.jar.Manifest.make72Safe()` source). Don't mistake a line that is
  *exactly* 72 characters for a violation; that's the correct maximum, not an
  overflow.

**A fourth gotcha that looked content-related but wasn't:** after fixing the jar
and confirming (via hash) it was correctly in place at the exact path
`bundles.info` references, the bundle still didn't appear. Cause: **restarting
via the Dock/Finder does not apply `-clean`** — only a real launch argument does
(`Eclipse.app/Contents/MacOS/eclipse -clean`, or `open -n Eclipse.app --args
-clean`). Without it, Equinox's cached bundle state from the earlier broken jar
persisted across "restarts" that weren't actually clean ones. A proper
Terminal-launched `-clean` resolved it immediately. Diagnostic tip for this
class of problem: Help → About → Installation Details → **Configuration** tab
(not Plug-ins) shows bundles stuck `INSTALLED` (unresolved) with the actual
constraint-violation reason, when Plug-ins alone would just show the bundle
missing.

**A fifth, UI-only gotcha after the import itself succeeded:** the imported
snapshot trace appeared to have **no events** — but this was a false alarm.
LTTng UST trace directories are conventionally named `.../uid/<uid>/64-bit`, so
importing traces from two different sources (e.g. a batch-copied trace and a
freshly-imported snapshot) produces **two tree nodes both literally named
`64-bit`**. Clicking a *child view* (e.g. `LTTng-UST CallStack`) under one
trace can leave the Events editor showing whichever trace was *already open* in
that tab, not the one whose view you clicked — easy to misread as "the new
trace has no data." **Fix: double-click the trace element itself** (not a
child view under it) to force a fresh Events editor tab for that specific
trace. Confirmed working: the snapshot trace opened with its real event data
once opened this way.

### 13.5 Status: TraceCompass client fully working end-to-end (2026-07-16)

With 13.1–13.4 applied, all three TraceCompass-side goals from §9's Phase 3 are
met from an ordinary laptop against Tuolumne's hardened sshd:

- **Batch fetch** (`ssh://yan10@tuolumne.llnl.gov:22`, ordinary SFTP-style
  Fetch Remote Traces) — working.
- **On-demand "near-live" snapshots** — the mechanism that actually panned out
  for GUI live-ness (see §3 "Variant 1.5" and §8): a `--snapshot` session
  coexists with the app's normal tracing, `Record Snapshot` in the Control view
  (or `lttng snapshot record` from the shell) can be triggered at any time
  without stopping the running producer, and **Import works on an ACTIVE
  snapshot session** (unlike a `--live` session, which TraceCompass's Import
  only accepts once stopped — see §13.3's discovery that Import is a
  file-based/SFTP mechanism, not a continuous live-network reader, in this
  TraceCompass release). This is the practical alternative to chasing a
  true continuously-updating live view, which older mailing-list evidence
  suggests may never have been fully productized in the Control view's Import
  action.
- **peam XML analysis on a real trace** — pending final confirmation the
  OMPT time-graph view renders correctly on the imported snapshot (§6's open
  item); mechanically unblocked now that import works.

**Net effect on §10's recommendation:** the snapshot workflow is a third,
practical middle ground between Option 1 (rotate + relay, near-line) and
Option 2 (live, real-time) — same on-demand-flight-recorder spirit as
"Variant 1.5," and it is the one now proven to work smoothly with
TraceCompass's actual Import mechanics, rather than requiring the
still-unresolved question of whether a genuinely continuous live GUI view
exists in this release.
