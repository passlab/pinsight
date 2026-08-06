# PInsight Manifest — user guide

The manifest makes PInsight traces **self-describing**: every trace carries a
record of which build, configuration, binary, and process/GPU binding
produced it, plus a run identifier that ties all the pieces of one
experiment together. Six near-identical trace directories from a week of
runs stop being indistinguishable; a snapshot captured mid-run still tells
you what was running and under which config; binding mistakes (the launcher
*intended* one GPU map, the process *saw* another) become visible in the
trace instead of requiring a diagnostic cluster job.

Design and rationale: `pinsight-eval/docs/design/ws1_manifest_design.md (private dev repo)`. This page is the
user view.

## 1. The pieces, and where each one lives

| Piece | Scope | Where it is stored | Status |
|---|---|---|---|
| Manifest **bursts** (events) | per **process** | inside the CTF trace, like any other events | implemented |
| Effective-config **dump files** | per distinct config, per node | `$PINSIGHT_MANIFEST_DIR/pinsight_config.<hash>.txt` | implemented |
| `run_manifest.json` **sidecar** | per **run/experiment** | run-folder root, next to the per-node trace dirs | implemented (`scripts/pinsight-manifest.sh`) |
| Hardware/env blobs (lstopo, amd-smi, env dumps) | per **node** | `<run dir>/manifest/` | implemented (`scripts/pinsight-manifest.sh`) |

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

### 1.1 Run-folder layout and the three `manifest*` files

A fully-managed run folder looks like this:

```
<rundir>/
  run_manifest.json     # THE manifest — the tool-owned sidecar record (§9.3)
  user_manifest.json    # optional INPUT you write; merged into `user:` (§9.2)
  manifest.env          # 2-line env shim; source it to (re)export run identity
  manifest/             # the manifest's bulk attachments:
    lstopo.<host>.xml  amdsmi.<host>.txt  env.<host>.txt  node.<host>.json
    pinsight_config.<hash>.txt         #   effective-config dumps (§7)
  <per-node trace dirs ...>            # CTF traces — the bursts live in here
```

Three similarly-named files, three different roles — only one is actually
a manifest:

| File | Who writes it | Role |
|---|---|---|
| `run_manifest.json` | `pinsight-manifest.sh` | **The manifest** (output): the run-level record — job, software, user facts, per-node hardware refs, config dumps, trace integrity hashes. Structure in §9.3 |
| `user_manifest.json` | **you** (optional) | **Input**: structured facts only you know (campaign parameters, solver config). Merged verbatim into the manifest's `user:` section at `run` time; use it when flat `--kv key=value` args aren't enough |
| `manifest.env` | `pinsight-manifest.sh` | **Convenience shim** (not manifest content): the two `export PINSIGHT_RUN_ID=… / PINSIGHT_MANIFEST_DIR=…` lines, persisted so *another shell* can join the same run later (`. <rundir>/manifest.env`) — e.g. a debug session or a launcher split across scripts. The primary mechanism is `eval "$(… run …)"`; this file is its durable copy |

The **burst** structure (the in-trace half) is described in §3 (when bursts
are emitted, latest-wins consumption) and §5 (fields and keys); the
**sidecar** structure in §9.3.

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

From Python, the analysis toolkit reads manifests directly
(`analysis/pinsight_reader.py`):

```python
from pinsight_reader import expand_dirs, manifests, load_run_manifest
dirs = expand_dirs(["/path/to/run"])       # any mix of run/node/trace dirs
m  = manifests(dirs)                       # {(hostname, pid): {key: value}}
m0 = manifests(dirs, at_ns=t)              # facts as of timestamp t (epochs)
rm = load_run_manifest(dirs[0])            # run_manifest.json, found by ascent
```

`mpi_gpu_energy_report.py` uses these facts for its rank↔GPU-device mapping
(falling back to inference on manifest-less traces), and the TraceCompass
state provider stores every kv under `/manifest/<pid>/<key>` for views to
query. The events are plain CTF regardless — any consumer can apply the
latest-wins rule directly.

## 9. Producing the sidecar: `scripts/pinsight-manifest.sh`

Assembles the run-level record no process can write. Everything is
optional and fail-soft: a missing tool skips its file, a partial node
collection still finalizes, and a trace produced without any of this
remains fully usable (§1).

### 9.1 Subcommands and typical wiring

```bash
PM=<pinsight>/scripts/pinsight-manifest.sh

# 1. Job start: mint run_id, create <rundir>/manifest/, write
#    run_manifest.json, and export PINSIGHT_RUN_ID + PINSIGHT_MANIFEST_DIR
#    (also saved to <rundir>/manifest.env for later sourcing):
eval "$($PM run <rundir> --kv problem_size=100^3 --kv solver=BoomerAMG)"

# 2. Once per node (hardware/env collection; all best-effort):
flux exec -r all bash $PM node <rundir>      # or: srun --ntasks-per-node=1

# 3. Run the traced application (it inherits the two exported variables).

# 4. Post-run: merge node facts, reference the config dumps, and add
#    sha256 integrity hashes for every CTF trace dir found:
$PM finalize <rundir> --traces <rundir>/traces
```

`run` keeps stdout to the export lines only (that's what `eval` consumes);
all diagnostics go to stderr. Rotation-mode trace chunks each get their own
sha256 at `finalize` (any directory containing a CTF `metadata` file is
hashed independently).

### 9.2 The three `manifest*` files

See the table in §1.1: `run_manifest.json` is the tool's output (the
manifest itself); `user_manifest.json` is your optional structured input,
merged into the `user:` section (flat facts can just use `--kv`);
`manifest.env` is the persisted copy of the two exports for re-use from
other shells.

### 9.3 `run_manifest.json` structure

All sections optional; present iff knowable. From a real 2-node run:

```json
{
  "schema_version": 1,
  "run":   { "run_id": "f3QWwM3kBTg3-01a81be2",
             "created":  "2026-08-05T14:53:01-07:00",
             "finalized": "2026-08-05T14:54:12-07:00" },
  "job":   { "scheduler": "flux", "job_id": "f3QWwM3kBTg3" },
  "software": { "pinsight_git_rev": "2c11d42" },
  "user":  { "app": "mpi_sleeper", "problem_size": "100^3" },
  "nodes": {
    "tuolumne1017": {
      "kernel": "4.18.0-553...", "cpu_model": "AMD Instinct MI300A ...",
      "mem_total_kb": 526343792,
      "files": { "lstopo": "lstopo.tuolumne1017.xml",
                 "amdsmi": "amdsmi.tuolumne1017.txt",
                 "env":    "env.tuolumne1017.txt" },
      "trace_sha256": { "traces/tuolumne1017/ust/uid/54705/64-bit": "624e..." }
    }
  },
  "config_dumps": [ "pinsight_config.bb782e81ade0105c.txt" ],
  "traces": { "root": "...", "sha256": { "traces/tuolumne1017/...": "624e..." } }
}
```

`files` entries are relative to `<rundir>/manifest/`. The join to the
traces is `run.run_id` (every burst echoes it) and, per config epoch,
`config_dumps` ↔ each burst's `pinsight.config_hash`.

### 9.4 With and without a job scheduler

**Flux** (batch script): as in §9.1 — note that inside a batch script
`FLUX_JOB_ID` is not in the environment; the script queries
`flux getattr jobid` itself, so the run_id is still `<jobid>-<suffix>`.
Per-node collection via `flux exec -r all bash $PM node <rundir>`.

**Slurm** (sbatch script): identical wiring; `SLURM_JOB_ID` is read from
the environment, and per-node collection uses
`srun --ntasks-per-node=1 bash $PM node <rundir>`.

**No scheduler** (single node / plain `mpirun` / interactive):

```bash
eval "$($PM run ./myrun)"     # run_id falls back to a uuid
$PM node ./myrun              # this node's hardware/env
mpirun -np 4 env LD_PRELOAD=... myapp    # env is inherited by the ranks
$PM finalize ./myrun --traces ./myrun/traces
```

The only scheduler-dependent parts are the run_id prefix (job id vs uuid)
and how you fan `node` out across machines. And with **no script at all**,
the manifest still works at trace scope: ranks generate a provisional
run_id and unify it through `MPI_Init` (§4), config hashes still appear in
every burst — you lose only the run-level record (job facts, hardware
inventory, config *content*, integrity hashes).

### 9.5 Without python3 (best-effort mode)

The script's only soft dependency is `python3` (stdlib only; override with
`PYTHON3=<path>`), used for all JSON work — `libpinsight` itself needs
nothing. Where python3 is missing (minimal compute images), the script
degrades without losing anything: `run` still mints the run_id and prints
the exports (the parts traces depend on), and `run`/`node` write flat
`key=value` **fragments** (`manifest/run.frag`, `manifest/node.<host>.frag`)
instead of JSON — bash never emits JSON, so `run_manifest.json` is always
python-written and always valid. A no-python `finalize` computes the trace
hashes (they need the trace files, which may not survive) into
`manifest/trace_sha256.frag` using the same hash scheme. **Re-running
`finalize` on any machine with python3 then merges all fragments — plus the
deferred `user_manifest.json` — into the full `run_manifest.json`.**
Degradation is temporary, not lossy. Limitation: fragment `--kv` keys are
restricted to `[A-Za-z0-9_.-]` (others are skipped with a warning); values
are free-form.

Packaging note: the script is deliberately **one self-contained file** —
the python programs are embedded as *static* heredocs (quoted delimiters:
bash never substitutes into them; all data passes via arguments, so user
values cannot inject into the code) and piped to `python3 -`. Bash must be
the outer layer anyway (a python entry point couldn't run at all on the
python-less nodes this mode exists for), and a single file survives being
copied into job-script directories and harnesses with no companion-file
path resolution or version skew. File names/locations are identical in
both modes: `run_manifest.json` is simply *absent* (never renamed or
relocated) until a python-equipped `finalize` writes it; fragments are a
producer-side staging detail under `manifest/` that consumers never read.
