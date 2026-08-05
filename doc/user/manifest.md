# PInsight Manifest — user guide

The manifest makes PInsight traces **self-describing**: every trace carries a
record of which build, configuration, binary, and process/GPU binding
produced it, plus a run identifier that ties all the pieces of one
experiment together. Six near-identical trace directories from a week of
runs stop being indistinguishable; a snapshot captured mid-run still tells
you what was running and under which config; binding mistakes (the launcher
*intended* one GPU map, the process *saw* another) become visible in the
trace instead of requiring a diagnostic cluster job.

Design and rationale: `doc/design/ws1_manifest_design.md`. This page is the
user view.

## 1. The pieces, and where each one lives

| Piece | Scope | Where it is stored | Status |
|---|---|---|---|
| Manifest **bursts** (events) | per **process** | inside the CTF trace, like any other events | implemented |
| Effective-config **dump files** | per distinct config, per node | `$PINSIGHT_MANIFEST_DIR/pinsight_config.<hash>.txt` | implemented |
| `run_manifest.json` **sidecar** | per **run/experiment** | run-folder root, next to the per-node trace dirs | planned (WS1 Step 4) |
| Hardware/env blobs (lstopo, amd-smi, env dumps) | per **node** | `<run dir>/manifest/` | planned (WS1 Step 4) |

How the scopes fit together:

- **Per process:** each traced process emits manifest *bursts* — a
  `manifest_process` event followed by a set of `manifest_kv` (key, value)
  events sharing a `seq` number. Everything a process can know about itself
  is in its own bursts; nothing requires looking outside the trace.
- **Per node trace dir:** a node's CTF trace contains the bursts of every
  process that ran on that node — the trace directory is self-contained.
- **Per run:** all processes of one run share a `run_id` (see §4), so the
  per-node trace dirs of a multi-node run can always be grouped, and joined
  to the run-level sidecar files when those exist.

A trace **never depends on** the sidecar files: they enrich (hardware
inventory, job metadata, config *content*), but every analysis works from
the trace alone.

## 2. Turning it on

Manifest events are recorded **iff the LTTng session enables the provider**:

```bash
lttng enable-event -u 'pinsight_manifest_lttng_ust:*'
```

That is the on/off switch — deliberately session-level, so a whole session
uniformly has manifests or doesn't (there is no config on/off key). Without
the session enabling it, the tracepoints are no-op branches; with all
domains OFF and energy off, PInsight also arms no timers (zero ambient
activity).

Optional environment variables (normally set by the launcher script):

| Variable | Effect |
|---|---|
| `PINSIGHT_RUN_ID` | Use this string as the run identifier (otherwise one is generated — §4) |
| `PINSIGHT_MANIFEST_DIR` | Directory where the control thread writes the effective-config dump files. **Unset → no files are ever written** (trace still carries the config hash) |

## 3. When bursts are emitted

Each burst's `manifest_process` event carries a `reason`:

| `reason` | When | What's notable in it |
|---|---|---|
| `init` | library constructor, before the app-start marker | rank is a best-effort guess from launcher env; run_id may still be provisional |
| `mpi_init` | inside the `MPI_Init` wrapper | **authoritative** `mpirank`; run_id unified across ranks |
| `periodic` | every `interval` seconds (§6) from the control thread | keeps every snapshot/rotation window self-contained |
| `window` | after every config reload and window/mode transition | re-stamps the new epoch (new `config_hash`, `window_gen`) |
| `fini` | at exit, before the app-end marker | last word |

Every burst repeats the **full** key set (no deltas): any captured window
containing one complete burst per process is fully self-describing, no
matter what the ring buffer evicted before it.

**Consumption rule: latest-wins per key.** For "what is true about this
process," take the newest value of each key you can see. For "what was true
during this window" (config epochs), take the newest burst at-or-before the
window. Bursts are idempotent; duplicates are by design.

## 4. The run identifier

`run_id` names the experiment. Where it comes from, in order:

1. **Launcher-provided:** `PINSIGHT_RUN_ID` exported before launch (the
   future `pinsight-manifest.sh` does this; a job script can too, e.g. from
   the scheduler job id). All ranks inherit the same value.
2. **MPI-unified:** with no env, each process generates a random id at
   startup, and rank 0's is broadcast to everyone inside `MPI_Init` — so a
   multi-node MPI run still ends up with one experiment-wide id, no
   launcher cooperation needed. (The `init` bursts show the per-rank
   provisional ids; `mpi_init` onward shows the unified one — latest-wins.)
3. **Standalone:** non-MPI, no env → the per-process random id stands.

Resulting keys: experiment = `run_id`; node trace = (`run_id`, `hostname`);
process = (`run_id`, `hostname`, `pid`).

## 5. What information a burst contains

`manifest_process` fields: `hostname`, `pid` (common to all PInsight
events), `seq`, `reason`, `mpirank`, `exe`, `window_gen` (which cyclic
tracing window this burst belongs to), `nprocs_hint` (job size from
launcher env, −1 unknown).

`manifest_kv` keys (each optional — a key is absent when unknowable):

| Key | Meaning |
|---|---|
| `run_id` | experiment identifier (§4) |
| `pinsight.version` | PInsight version that produced the trace |
| `pinsight.domains` | domains compiled into this build, e.g. `OpenMP,MPI,HIP,energy` |
| `pinsight.config_hash` | hash of the **effective** configuration (§7) — the join key to the config dump file |
| `launcher.job_id` | `FLUX_JOB_ID` / `SLURM_JOB_ID` as inherited |
| `launcher.rank` | rank per launcher env (`PMI_RANK`/`FLUX_TASK_RANK`/…) |
| `gpu.rocr_visible_devices`, `gpu.hip_visible_devices`, `gpu.cuda_visible_devices` | the visibility env **as this process actually saw it** |
| `gpu.physical_dev_offset` | the devId offset PInsight applies (first index in the ROCR/HIP list) |
| `cpu.affinity` | CPU binding as a range list, e.g. `0-23,96` |
| `omp.num_threads` | `OMP_NUM_THREADS` as seen |
| `host.exe` | resolved executable path (`/proc/self/exe`) |
| `host.exe_build_id` | GNU build-id of the executable — identifies the exact binary even when paths are reused |
| `host.cwd`, `host.cmdline` | working directory and command line (truncated at 256 bytes) |

Notes: values are capped at 256 bytes (`...` marks truncation). If the
"executable" is a script, `host.exe`/build-id describe the interpreter
(true for that process); the application identity is in `host.cmdline`,
and the actual app binary appears in its own process's bursts.

## 6. Configuration: `[Manifest]` section

```ini
[Manifest]
    interval = 10     # seconds between periodic bursts; 0 = lifecycle bursts only
```

- Default `10`. Reloadable at runtime (SIGUSR1 config reload).
- **Windowed captures (snapshot/rotation): set `interval` below half the
  shortest window**, so every captured chunk contains at least one complete
  burst per process. `interval = 0` is only appropriate for plain
  batch captures.
- Periodic bursts pause automatically while nothing is being collected
  (all domain modes `OFF` and `[Energy] measure = off`) — the *dormancy
  rule*; lifecycle bursts (`init`/`mpi_init`/`window`/`fini`) always fire.
- Cost: a burst is ~2 KB; at the default interval that is ~200 B/s per
  process — negligible, and periodic emission runs on PInsight's control
  thread, never on application threads.

## 7. The effective-config record

Every burst's `pinsight.config_hash` identifies the configuration **in
effect at that moment** — computed from PInsight's in-memory state (defaults
applied, env overrides, reload results), not from the config file's bytes.
Two runs share a hash iff their effective configs were identical;
comment-only file edits don't change it; a mid-run reload gives later bursts
a new hash, so config *epochs* are visible on the trace timeline.

With `PINSIGHT_MANIFEST_DIR` set, the config **content** behind each hash is
written once per node to:

```
$PINSIGHT_MANIFEST_DIR/pinsight_config.<hash>.txt
```

(content-addressed: N ranks and repeated runs of the same config produce one
file; a run with one reload produces two). To answer "what config governed
this part of the trace": read the burst's hash, open the matching file. The
dump is in normal config-file syntax and can be re-used as a config file.

Without `PINSIGHT_MANIFEST_DIR`, no files are written; hashes still support
"same or different config?" comparisons between runs.

## 8. Reading manifests

Quick look with babeltrace2:

```bash
babeltrace2 <trace dir> | grep manifest_
# just the identity of every process:
babeltrace2 <trace dir> | grep manifest_process
# what config epochs exist:
babeltrace2 <trace dir> | grep 'pinsight.config_hash' | sort -u
```

Example burst (abridged):

```
... manifest_process: { hostname = "tuolumne2151", pid = 601142, seq = 1,
                        reason = "mpi_init", mpirank = 3, exe = "/g/.../amg",
                        window_gen = 0, nprocs_hint = 16 }
... manifest_kv: { ... seq = 1, key = "run_id", value = "f58BxjV3-a3f9c2" }
... manifest_kv: { ... seq = 1, key = "gpu.rocr_visible_devices", value = "3" }
... manifest_kv: { ... seq = 1, key = "cpu.affinity", value = "72-95" }
```

Analysis-toolkit integration (`analysis/pinsight_reader.py` helpers,
TraceCompass row labels) is WS1 Step 5 — until then, the events are plain
CTF and any consumer can apply the latest-wins rule directly.

## 9. Coming with WS1 Step 4 (launcher sidecar)

`scripts/pinsight-manifest.sh` will assemble the run-level record no process
can write: `run_manifest.json` (job metadata, campaign parameters,
user-provided facts via `--kv key=value`, per-node `trace_sha256` integrity
hashes) plus per-node hardware blobs (lstopo XML, `amd-smi static`, env
dumps) under `<run dir>/manifest/`, and will export `PINSIGHT_RUN_ID` /
`PINSIGHT_MANIFEST_DIR` so the in-trace side joins to it automatically. A
trace without any of this remains fully usable (§1).
