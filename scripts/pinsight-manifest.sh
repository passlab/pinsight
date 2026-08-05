#!/bin/bash
# pinsight-manifest.sh — launcher-side manifest collector (WS1 Step 4).
# Design: doc/design/ws1_manifest_design.md §3; user guide: doc/user/manifest.md.
#
# The in-trace manifest (bursts) never depends on this script; it ENRICHES a
# run with the record no single process can write: job/campaign facts, per-node
# hardware inventory, user-provided facts, and post-run trace integrity hashes.
#
# Subcommands (typical launcher wiring shown in doc/user/manifest.md):
#
#   run <rundir> [--kv key=value ...]
#       Initialize the run manifest: mint run_id (scheduler job id + random
#       suffix, else uuid), create <rundir>/manifest/, write run_manifest.json
#       (job env, software provenance, user kvs, user_manifest.json merge).
#       Prints eval-able export lines (also saved to <rundir>/manifest.env):
#           eval "$(pinsight-manifest.sh run <rundir>)"
#       Exports: PINSIGHT_RUN_ID (echoed by every rank's bursts — the
#       trace<->sidecar join key) and PINSIGHT_MANIFEST_DIR=<rundir>/manifest
#       (where PInsight's control thread writes pinsight_config.<hash>.txt).
#
#   node <rundir>
#       Per-node hardware/env collection into <rundir>/manifest/ — run ONCE
#       PER NODE (e.g. `flux exec -r all` / `srun --ntasks-per-node=1`).
#       Everything best-effort: a missing tool skips its file, never fails.
#
#   finalize <rundir> [--traces <dir>]
#       Post-run: merge node.*.json into run_manifest.json, record the
#       config-dump files, and add sha256 integrity hashes for every CTF
#       trace dir (any dir containing a `metadata` file under --traces,
#       default <rundir> — rotation chunks each get their own entry).
#
# All diagnostics go to stderr; `run` keeps stdout = export lines only.
#
# NO-PYTHON BEST-EFFORT MODE: run_manifest.json (the canonical manifest) is
# only ever written by python3 — bash never emits JSON (unescapable user
# values could corrupt it). Without python3, `run`/`node` write flat
# `key=value` FRAGMENTS (manifest/run.frag, manifest/node.<host>.frag) and
# `finalize` computes trace hashes into manifest/trace_sha256.frag; nothing
# is lost, and re-running `finalize` on any machine WITH python3 merges all
# fragments (+ the deferred user_manifest.json) into the full JSON. The
# critical path — run_id minting, env exports, hardware blobs, hashes —
# never needs python at all.
set -u

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
PY=${PYTHON3:-python3}
HAVE_PY=1
command -v "$PY" >/dev/null 2>&1 || HAVE_PY=0
[ "$HAVE_PY" = 1 ] || echo "pinsight-manifest: python3 not found — best-effort mode:" \
  "writing flat fragments; re-run 'finalize' where python3 exists to produce run_manifest.json" >&2

die() { echo "pinsight-manifest: $*" >&2; exit 1; }

cmd=${1:-}
[ -n "$cmd" ] || die "usage: pinsight-manifest.sh run|node|finalize <rundir> [options]"
shift

case "$cmd" in
# ============================================================ run
run)
  rundir=${1:-}; [ -n "$rundir" ] || die "run: missing <rundir>"
  shift
  mkdir -p "$rundir/manifest" || die "run: cannot create $rundir/manifest"
  rundir=$(cd "$rundir" && pwd)

  # run_id (design §2.5): scheduler job id + short random suffix -> readable
  # and site-unique, suffix guards job-id reuse; else uuid. Exported so every
  # rank's bursts echo it (tier 1; without it ranks fall back to getrandom +
  # MPI_Init unification). Flux note: a batch script's env has no FLUX_JOB_ID;
  # the id comes from `flux getattr jobid` inside the allocation.
  jid=${FLUX_JOB_ID:-${SLURM_JOB_ID:-}}
  [ -n "$jid" ] || jid=$(flux getattr jobid 2>/dev/null || true)
  [ "$jid" != "0" ] || jid=""  # a personal `flux start` instance reports 0
  suf=$(od -An -N4 -tx1 /dev/urandom 2>/dev/null | tr -d ' \n')
  if [ -n "$jid" ]; then
    run_id="${jid}-${suf:-0}"
  else
    run_id=$(uuidgen 2>/dev/null || echo "${suf:-0}-$(date +%s)")
  fi

  # PInsight source rev (this script lives in <pinsight>/scripts) — optional.
  pinsight_rev=$(git -C "$SCRIPT_DIR/.." rev-parse --short HEAD 2>/dev/null || true)

  if [ "$HAVE_PY" = 0 ]; then
    # Fallback: flat fragment (dotted keys mirror the JSON paths); a later
    # python-equipped `finalize` merges it into run_manifest.json.
    frag="$rundir/manifest/run.frag"
    {
      echo "schema_version=1"
      echo "run.run_id=$run_id"
      echo "run.created=$(date -Iseconds 2>/dev/null || date)"
      if [ -n "${SLURM_JOB_ID:-}" ]; then echo "job.scheduler=slurm"
      elif [ -n "$jid" ]; then echo "job.scheduler=flux"; fi
      [ -z "$jid" ] || echo "job.job_id=$jid"
      [ -z "$pinsight_rev" ] || echo "software.pinsight_git_rev=$pinsight_rev"
    } > "$frag"
    while [ $# -gt 0 ]; do
      if [ "$1" = "--kv" ] && [ $# -ge 2 ]; then
        kv=$2; k=${kv%%=*}; v=${kv#*=}
        if [ -n "$k" ] && [ "$k" != "$kv" ] && printf '%s' "$k" | grep -Eq '^[A-Za-z0-9_.-]+$'; then
          printf 'user.%s=%s\n' "$k" "$(printf '%s' "$v" | tr '\n' ' ')" >> "$frag"
        else
          echo "pinsight-manifest: warning: skipped --kv '$kv' (key must be [A-Za-z0-9_.-]+)" >&2
        fi
        shift 2
      else
        echo "pinsight-manifest: warning: ignored argument '$1'" >&2
        shift
      fi
    done
    [ ! -f "$rundir/user_manifest.json" ] || echo "pinsight-manifest:" \
      "user_manifest.json merge deferred to a python3-equipped 'finalize'" >&2
    echo "pinsight-manifest: wrote $frag (run_id $run_id; no-python mode)" >&2
  else
  "$PY" - "$rundir" "$run_id" "$pinsight_rev" "$jid" "$@" <<'PYEOF' 1>&2 || die "run: JSON assembly failed"
import json, os, sys, datetime
rundir, run_id, rev, jid, *rest = sys.argv[1:]

user = {}
um = os.path.join(rundir, "user_manifest.json")
if os.path.exists(um):
    try:
        user.update(json.load(open(um)))
    except Exception as e:
        print(f"pinsight-manifest: warning: user_manifest.json ignored: {e}",
              file=sys.stderr)
i = 0
while i < len(rest):
    if rest[i] == "--kv" and i + 1 < len(rest) and "=" in rest[i + 1]:
        k, v = rest[i + 1].split("=", 1)
        user[k] = v
        i += 2
    else:
        print(f"pinsight-manifest: warning: ignored argument {rest[i]!r}",
              file=sys.stderr)
        i += 1

env = os.environ.get
job = {k: v for k, v in {
    "scheduler": "slurm" if env("SLURM_JOB_ID") else ("flux" if (jid or env("FLUX_URI")) else None),
    "job_id": jid or None,
    "nodes": env("SLURM_JOB_NUM_NODES") or env("FLUX_JOB_NNODES"),
    "nodelist": env("SLURM_JOB_NODELIST"),
    "queue": env("SLURM_JOB_PARTITION"),
}.items() if v}

doc = {
    "schema_version": 1,
    "run": {
        "run_id": run_id,
        "created": datetime.datetime.now().astimezone().isoformat(timespec="seconds"),
    },
    "job": job,
    "software": {k: v for k, v in {"pinsight_git_rev": rev or None}.items() if v},
    "user": user,
    "nodes": {},
}
out = os.path.join(rundir, "run_manifest.json")
json.dump(doc, open(out, "w"), indent=2)
print(f"pinsight-manifest: wrote {out} (run_id {run_id})", file=sys.stderr)
PYEOF
  fi

  envfile="$rundir/manifest.env"
  {
    echo "export PINSIGHT_RUN_ID=\"$run_id\""
    echo "export PINSIGHT_MANIFEST_DIR=\"$rundir/manifest\""
  } > "$envfile"
  cat "$envfile"   # stdout: eval-able by the launcher
  ;;

# ============================================================ node
node)
  rundir=${1:-}; [ -n "$rundir" ] || die "node: missing <rundir>"
  m="$rundir/manifest"
  mkdir -p "$m" || die "node: cannot create $m"
  h=$(hostname)

  # Bulk blobs — every one best-effort (design: partial failure tolerated).
  if command -v lstopo-no-graphics >/dev/null 2>&1; then
    lstopo-no-graphics --of xml > "$m/lstopo.$h.xml" 2>/dev/null || rm -f "$m/lstopo.$h.xml"
  elif command -v lstopo >/dev/null 2>&1; then
    lstopo --of xml > "$m/lstopo.$h.xml" 2>/dev/null || rm -f "$m/lstopo.$h.xml"
  fi
  if command -v amd-smi >/dev/null 2>&1; then
    amd-smi static > "$m/amdsmi.$h.txt" 2>/dev/null || rm -f "$m/amdsmi.$h.txt"
  elif command -v rocm-smi >/dev/null 2>&1; then
    rocm-smi --showhw > "$m/amdsmi.$h.txt" 2>/dev/null || rm -f "$m/amdsmi.$h.txt"
  fi
  if command -v nvidia-smi >/dev/null 2>&1; then
    nvidia-smi -q > "$m/nvidiasmi.$h.txt" 2>/dev/null || rm -f "$m/nvidiasmi.$h.txt"
  fi
  env | sort > "$m/env.$h.txt" 2>/dev/null || rm -f "$m/env.$h.txt"

  if [ "$HAVE_PY" = 0 ]; then
    # Fallback: flat fragment; merged into run_manifest.json by a later
    # python-equipped `finalize`. Values here are machine-derived.
    frag="$m/node.$h.frag"
    {
      echo "hostname=$h"
      echo "kernel=$(uname -r 2>/dev/null || true)"
      cm=$(grep -m1 -E '^model name' /proc/cpuinfo 2>/dev/null | sed 's/^[^:]*:[[:space:]]*//')
      [ -z "$cm" ] || echo "cpu_model=$cm"
      mt=$(grep -m1 -oE '[0-9]+' /proc/meminfo 2>/dev/null)
      [ -z "$mt" ] || echo "mem_total_kb=$mt"
      for spec in "lstopo:lstopo.$h.xml" "amdsmi:amdsmi.$h.txt" \
                  "nvidiasmi:nvidiasmi.$h.txt" "env:env.$h.txt"; do
        key=${spec%%:*}; name=${spec#*:}
        [ ! -f "$m/$name" ] || echo "files.$key=$name"
      done
    } > "$frag"
    echo "pinsight-manifest: wrote $frag (no-python mode)" >&2
  else
  "$PY" - "$m" "$h" <<'PYEOF' 1>&2 || die "node: JSON assembly failed"
import json, os, re, sys
m, h = sys.argv[1:3]

def first_match(path, pat):
    try:
        for line in open(path):
            mm = re.search(pat, line)
            if mm:
                return mm.group(1).strip()
    except Exception:
        pass
    return None

node = {"hostname": h}
try:
    node["kernel"] = os.uname().release
except Exception:
    pass
v = first_match("/proc/cpuinfo", r"model name\s*:\s*(.+)")
if v: node["cpu_model"] = v
v = first_match("/proc/meminfo", r"MemTotal:\s*(\d+)")
if v: node["mem_total_kb"] = int(v)

files = {}
for key, name in (("lstopo", f"lstopo.{h}.xml"), ("amdsmi", f"amdsmi.{h}.txt"),
                  ("nvidiasmi", f"nvidiasmi.{h}.txt"), ("env", f"env.{h}.txt")):
    if os.path.exists(os.path.join(m, name)):
        files[key] = name
if files:
    node["files"] = files

out = os.path.join(m, f"node.{h}.json")
json.dump(node, open(out, "w"), indent=2)
print(f"pinsight-manifest: wrote {out}", file=sys.stderr)
PYEOF
  fi
  ;;

# ============================================================ finalize
finalize)
  rundir=${1:-}; [ -n "$rundir" ] || die "finalize: missing <rundir>"
  shift
  traces="$rundir"
  if [ "${1:-}" = "--traces" ]; then
    traces=${2:-}; [ -n "$traces" ] || die "finalize: --traces needs a dir"
  fi
  [ -f "$rundir/run_manifest.json" ] || [ -f "$rundir/manifest/run.frag" ] || \
    die "finalize: neither run_manifest.json nor manifest/run.frag found (run 'run' first)"

  if [ "$HAVE_PY" = 0 ]; then
    # Fallback: compute the trace integrity hashes now (they need the trace
    # files, which may be purged later) into a sha256sum-style fragment; the
    # JSON merge waits for a python3-equipped re-run of `finalize`.
    # Dir-hash scheme MUST match the python one: sha256 over the sorted
    # "relname:sha256(file)" lines, joined by \n with NO trailing newline.
    m="$rundir/manifest"
    frag="$m/trace_sha256.frag"
    : > "$frag"
    find "$traces" -name metadata -type f 2>/dev/null | while read -r meta; do
      d=$(dirname "$meta")
      case "$(cd "$d" && pwd)" in "$(cd "$m" && pwd)"*) continue;; esac
      rel=${d#"$traces"/}; [ "$rel" != "$d" ] || rel=$(basename "$d")
      hash=$( (cd "$d" && find . -type f | sed 's|^\./||' | LC_ALL=C sort | \
               while read -r f; do
                 printf '%s:%s\n' "$f" "$(sha256sum "$f" | cut -d' ' -f1)"
               done) | LC_ALL=C sort | \
              awk 'NR>1{printf "\n"} {printf "%s", $0}' | sha256sum | cut -d' ' -f1 )
      printf '%s  %s\n' "$hash" "$rel" >> "$frag"
    done
    n=$(wc -l < "$frag" 2>/dev/null || echo 0)
    echo "pinsight-manifest: wrote $frag ($n trace dir(s); no-python mode)." \
         "Re-run 'finalize' where python3 exists to produce run_manifest.json" \
         "— all inputs are preserved." >&2
  else
  "$PY" - "$rundir" "$traces" <<'PYEOF' 1>&2 || die "finalize: failed"
import datetime, glob, hashlib, json, os, sys
rundir, traces = sys.argv[1:3]
m = os.path.join(rundir, "manifest")

def load_frag(path):
    """Flat key=value fragment written by the no-python fallback."""
    d = {}
    try:
        for line in open(path):
            line = line.rstrip("\n")
            if line and "=" in line and not line.startswith("#"):
                k, v = line.split("=", 1)
                d[k] = v
    except Exception as e:
        print(f"pinsight-manifest: warning: {path} ignored: {e}", file=sys.stderr)
    return d

mf = os.path.join(rundir, "run_manifest.json")
if os.path.exists(mf):
    doc = json.load(open(mf))
else:
    # `run` executed in no-python mode: build the doc from its fragment and
    # perform the deferred user_manifest.json merge.
    fr = load_frag(os.path.join(m, "run.frag"))
    doc = {"schema_version": 1, "run": {}, "job": {}, "software": {},
           "user": {}, "nodes": {}}
    for k, v in fr.items():
        if k == "schema_version":
            continue
        if "." in k:
            sec, kk = k.split(".", 1)
            doc.setdefault(sec, {})[kk] = v
    um = os.path.join(rundir, "user_manifest.json")
    if os.path.exists(um):
        try:
            merged = json.load(open(um))
            merged.update(doc.get("user", {}))  # --kv wins over the file
            doc["user"] = merged
        except Exception as e:
            print(f"pinsight-manifest: warning: user_manifest.json ignored: {e}",
                  file=sys.stderr)

# 1. Merge per-node collections (JSON from python-mode `node`, fragments
#    from no-python-mode `node`; JSON wins where both exist).
nodes = doc.setdefault("nodes", {})
for nf in sorted(glob.glob(os.path.join(m, "node.*.frag"))):
    fr = load_frag(nf)
    host = fr.get("hostname") or os.path.basename(nf)[5:-5]
    n = nodes.setdefault(host, {})
    for k, v in fr.items():
        if k.startswith("files."):
            n.setdefault("files", {})[k[6:]] = v
        elif k == "mem_total_kb":
            try:
                n[k] = int(v)
            except ValueError:
                n[k] = v
        elif v:
            n[k] = v
for nf in sorted(glob.glob(os.path.join(m, "node.*.json"))):
    try:
        n = json.load(open(nf))
        nodes.setdefault(n.get("hostname", os.path.basename(nf)), {}).update(n)
    except Exception as e:
        print(f"pinsight-manifest: warning: {nf} ignored: {e}", file=sys.stderr)

# 2. Reference the effective-config dumps the control threads wrote here.
dumps = sorted(os.path.basename(p)
               for p in glob.glob(os.path.join(m, "pinsight_config.*.txt")))
if dumps:
    doc["config_dumps"] = dumps

# 3. Integrity hash per CTF trace dir: any dir under <traces> containing a
#    `metadata` file (rotation chunks each contain one -> per-chunk hashes).
#    Dir hash = sha256 over "relname:sha256(file)" lines, sorted — stable
#    against listing order.
def dir_sha256(d):
    entries = []
    for root, _, fns in os.walk(d):
        for fn in fns:
            p = os.path.join(root, fn)
            hh = hashlib.sha256()
            with open(p, "rb") as f:
                for chunk in iter(lambda: f.read(1 << 20), b""):
                    hh.update(chunk)
            entries.append(f"{os.path.relpath(p, d)}:{hh.hexdigest()}")
    top = hashlib.sha256("\n".join(sorted(entries)).encode())
    return top.hexdigest()

trace_hashes = {}
for root, dirs, fns in os.walk(traces):
    if os.path.abspath(root).startswith(os.path.abspath(m)):
        dirs[:] = []          # never hash the manifest dir itself
        continue
    if "metadata" in fns:
        rel = os.path.relpath(root, traces)
        trace_hashes[rel] = dir_sha256(root)
        dirs[:] = []          # a CTF dir is a leaf for our purposes
if not trace_hashes:
    # Traces gone/elsewhere (e.g. purged node-local storage) but a no-python
    # finalize recorded their hashes earlier: use that fragment.
    tf = os.path.join(m, "trace_sha256.frag")
    if os.path.exists(tf):
        for line in open(tf):
            parts = line.rstrip("\n").split("  ", 1)
            if len(parts) == 2 and parts[0]:
                trace_hashes[parts[1]] = parts[0]
if trace_hashes:
    doc["traces"] = {"root": os.path.abspath(traces),
                     "sha256": trace_hashes}
    # Attach to nodes when the path names the host (our per-node layout).
    for host in nodes:
        mine = {r: h for r, h in trace_hashes.items() if host in r}
        if mine:
            nodes[host]["trace_sha256"] = mine

doc["run"]["finalized"] = datetime.datetime.now().astimezone().isoformat(timespec="seconds")
out = os.path.join(rundir, "run_manifest.json")
json.dump(doc, open(out, "w"), indent=2)
print(f"pinsight-manifest: finalized {out} "
      f"({len(nodes)} node(s), {len(trace_hashes)} trace dir(s), "
      f"{len(dumps)} config dump(s))", file=sys.stderr)
PYEOF
  fi
  ;;

*)
  die "unknown subcommand '$cmd' (expected run|node|finalize)"
  ;;
esac
