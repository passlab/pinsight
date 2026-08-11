# Running PInsight under a Job Scheduler (Flux, Slurm)

PInsight itself is scheduler-agnostic: it is an `LD_PRELOAD` library plus a
userspace LTTng session. What changes under a scheduler is *where the LTTng
session daemon lives, who starts it, and how long it survives* — getting that
wrong is the most common cause of "job ran fine, trace is empty". This guide
distills the launch patterns that have been validated on production clusters
(Flux on an MI300A system, Slurm on an H100 system) into generic recipes.

For the run-level provenance sidecar (`pinsight-manifest.sh`) see
[manifest.md](manifest.md) §9 — its wiring is shown briefly in each recipe
below.

## 1. The four invariants

Every working multi-node launch obeys these, regardless of scheduler:

1. **One LTTng session (and sessiond) per node.** LTTng-UST is node-local:
   ranks on a node trace into that node's session. Give each node's session
   a unique name and output directory, e.g. `--output=$OUT/$(hostname)`;
   traces then land per node and are grouped afterwards (the manifest's
   `run_id` joins them).

2. **`LTTNG_HOME` must be node-local.** On clusters `$HOME` is typically
   NFS-shared; lttng-sessiond keeps its lock file and sockets under
   `$LTTNG_HOME` (default `$HOME`), and concurrent nodes' daemons collide on
   the shared lock. Always set something like
   `LTTNG_HOME=/tmp/$USER/lttng-$JOBID` — for **both** the `lttng` CLI calls
   and the traced application (the preloaded library's tracepoints must find
   the same daemon).

3. **The sessiond's lifetime must span the application run — scope it to the
   same job step.** Schedulers wrap each launched task in a cgroup and kill
   everything in it (daemonized or not) when the task's command exits. A
   sessiond started in its own `flux run` / `srun` invocation is dead before
   the application step even starts — the session gets created successfully,
   and zero events are captured. Either start the daemon in the batch
   script's own shell (single-node case) or fold create/start → app →
   stop/destroy into *one* launched task per rank (multi-node case, §4).

4. **Session setup can be idempotent instead of coordinated.** When every
   rank on a node runs the same `lttng create/enable/start` sequence, only
   the fastest rank's attempt matters: a duplicate-name `create` fails
   harmlessly, as does destroying an already-destroyed session. No node-local
   lock or leader election is needed. (Teardown is safe for MPI apps because
   the collective `MPI_Finalize` keeps local ranks' completion times close —
   the first finisher's `lttng stop` does not cut off meaningful trailing
   events.)

## 2. No scheduler: plain `mpirun`, single node

The simple case — one node, so one session, started before the launch:

```bash
PM=<pinsight>/scripts/pinsight-manifest.sh
eval "$($PM run ./myrun)"          # optional: mints PINSIGHT_RUN_ID/_MANIFEST_DIR
$PM node ./myrun                   # optional: this node's hardware/env

lttng create myrun --output=./myrun/traces/$(hostname)
lttng enable-event -u 'pmpi_pinsight_lttng_ust:*'          # …and the other
lttng enable-event -u 'pinsight_manifest_lttng_ust:*'      # providers you built
lttng start

mpirun -np 4 env LD_PRELOAD=<pinsight>/build/libpinsight.so ./myapp

lttng stop && lttng destroy
$PM finalize ./myrun --traces ./myrun/traces               # optional
```

`env LD_PRELOAD=... ./myapp` (rather than exporting `LD_PRELOAD` globally)
keeps the preload scoped to the application ranks — `mpirun`, `lttng`, and
other tooling stay un-instrumented.

## 3. Slurm, single node (`sbatch`)

With `-N 1`, the batch script itself runs on the (only) compute node, so its
shell can own the daemon and session directly — invariant 3 is satisfied
because the batch shell outlives every `srun` step:

```bash
#!/bin/bash
#SBATCH -N 1 -n 4 --gres=gpu:4 -t 15 ...

export LTTNG_HOME=/tmp/$USER/lttng-$SLURM_JOB_ID     # invariant 2
mkdir -p $LTTNG_HOME
lttng-sessiond --daemonize && sleep 2

PM=<pinsight>/scripts/pinsight-manifest.sh           # optional manifest wiring
eval "$($PM run $RUNDIR)"; $PM node $RUNDIR

# Per-rank wrapper: anything else you want logged per rank goes here too.
WRAP=$LTTNG_HOME/rankwrap.sh
cat > $WRAP <<'EOF'
#!/bin/bash
exec env LD_PRELOAD=$PINSIGHT_LIB "$@"
EOF
chmod +x $WRAP

lttng create myrun --output=$RUNDIR/traces/$(hostname)
lttng enable-event -u 'pmpi_pinsight_lttng_ust:*'    # …etc.
lttng start
srun -n4 $WRAP ./myapp args...
lttng stop && lttng destroy

$PM finalize $RUNDIR --traces $RUNDIR/traces
```

The wrapper-script pattern (`exec env LD_PRELOAD=... "$@"`) is preferred over
`export LD_PRELOAD` before `srun` for the same scoping reason as in §2, and
it survives Slurm configurations that sanitize the step environment.

## 4. Multi-node: the combined-step pattern (Flux or Slurm)

Across nodes, the batch shell runs on only one node, so it cannot own the
other nodes' sessions — and a separate per-node "start the daemon" step dies
with its cgroup (invariant 3). The robust pattern folds *everything into the
single launched task that every rank runs*: idempotent session setup, the
application, idempotent teardown. Flux version (validated at 4 nodes ×
4 APUs; the `srun` version is identical in structure but **not yet confirmed
on a multi-node Slurm run** — if you try it, please report back):

```bash
#!/bin/bash
# inside the batch allocation (flux batch ... job.sh / sbatch job.sh)
OUT=...          # shared filesystem; traces land in $OUT/<hostname>/

flux run -N$NODES -n$NRANKS bash -c '            # srun -N... -n... for Slurm
  H=$(hostname)
  export LTTNG_HOME=/tmp/$USER/lttng_home        # node-local (invariant 2)
  mkdir -p $LTTNG_HOME

  # Idempotent per-node session setup (invariant 4): every local rank tries,
  # first one wins, the rest fail harmlessly.
  lttng create run-$H --output=$OUT/$H            >/dev/null 2>&1
  lttng enable-event -u "pmpi_pinsight_lttng_ust:*"       >/dev/null 2>&1
  lttng enable-event -u "pinsight_manifest_lttng_ust:*"   >/dev/null 2>&1
  # ...remaining providers...
  lttng start                                     >/dev/null 2>&1

  env LD_PRELOAD=$PINSIGHT_LIB PINSIGHT_TRACE_CONFIG_FILE=$CONFIG ./myapp args...

  lttng stop            >/dev/null 2>&1
  lttng destroy run-$H  >/dev/null 2>&1
  true
'
```

Notes:

- The first `lttng` call auto-spawns the per-node sessiond under the
  node-local `LTTNG_HOME`; because it lives in the same task tree as the
  application ranks, it survives exactly as long as needed.
- On Cray PE systems the inherited environment (`LD_LIBRARY_PATH` full of
  `/opt/cray/...`) can make lttng-sessiond fail with *"cannot allocate memory
  in static TLS block"*. Run the `lttng` CLI under a clean environment
  (`env -i HOME=$HOME PATH=... LD_LIBRARY_PATH=<lttng-lib-dir> LTTNG_HOME=...`)
  — unsetting `LD_PRELOAD` alone is not enough.
- Quoting: the whole per-rank script is one quoted string inside the batch
  heredoc; a stray apostrophe *even inside a comment* silently truncates it.
- Manifest wiring is the same as §3, from the batch script:
  `eval "$($PM run $RUNDIR)"` before the launch (ranks inherit
  `PINSIGHT_RUN_ID`/`PINSIGHT_MANIFEST_DIR`), per-node collection with
  `flux exec -r all bash $PM node $RUNDIR` (Flux) or
  `srun --ntasks-per-node=1 bash $PM node $RUNDIR` (Slurm), and
  `$PM finalize` after the launch returns.

## 5. GPU binding under schedulers

Scheduler GPU binding interacts with tracing in two ways worth knowing:

- **Slurm `--gpus-per-task=1` collapses device IDs.** Slurm cgroup-masks each
  rank down to one GPU, so `cudaGetDevice()` — and therefore the `devId`
  recorded by CUPTI — reports `0` for *every* rank regardless of physical
  GPU. The trace is still correct per rank, but cross-rank device attribution
  must come from the manifest's binding facts (`CUDA_VISIBLE_DEVICES` /
  PCI bus id as seen by each rank), not from `devId` alone.
- **Verify automatic binding before trusting it.** On at least one production
  Flux instance, `flux run -g1` assigned every local rank the *same*
  `ROCR_VISIBLE_DEVICES` value instead of rotating across the node's GPUs.
  The portable fallback is explicit binding from the local rank index:

  ```bash
  export ROCR_VISIBLE_DEVICES=$((FLUX_TASK_RANK % RANKS_PER_NODE))   # Flux
  export CUDA_VISIBLE_DEVICES=$SLURM_LOCALID                         # Slurm
  ```

  The manifest's per-rank binding facts make such mistakes visible after the
  fact — enable the manifest provider on any run you may need to debug.

## 6. Troubleshooting

| Symptom | Likely cause | Fix |
|---------|--------------|-----|
| Session created, job ran, **zero events** | sessiond started in its own `flux run`/`srun` step and died with that step's cgroup | Combined-step pattern (§4), or daemon in the batch shell for single-node (§3) |
| `lttng create` errors about locks / "sessiond not responding" on multi-node runs | Shared `$HOME`: all nodes' daemons fighting over one `$LTTNG_HOME` lock file | Node-local `LTTNG_HOME` (invariant 2), for CLI **and** app |
| sessiond crashes with "cannot allocate memory in static TLS block" | Inherited Cray/PE `LD_LIBRARY_PATH` | Run `lttng` CLI under `env -i` (§4) |
| Every rank's trace shows `devId=0` | `--gpus-per-task=1` cgroup masking | Expected; recover physical mapping from manifest binding facts (§5) |
| All ranks ran on one GPU | Scheduler `-g1`-style binding not rotating devices | Explicit `*_VISIBLE_DEVICES` from local rank index (§5) |
