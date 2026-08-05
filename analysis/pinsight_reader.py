#!/usr/bin/env python3
# Shared reader for PInsight CTF traces (via babeltrace2 text output).
# Used by the analysis scripts in this folder. App-agnostic.
#
# Feed it one or more trace directories (e.g. the per-node output dirs of a
# multi-node run); babeltrace2 merges them into one time-ordered stream.
#
#   from pinsight_reader import events
#   for ev in events(["/path/node1", "/path/node2"]):
#       ev.t_ns, ev.host, ev.provider, ev.name, ev.fields (dict of str->str)
import re, subprocess, sys
from dataclasses import dataclass

_ts_re  = re.compile(r'^\[(\d\d):(\d\d):(\d\d)\.(\d{9})\]')
# All PInsight providers end in _lttng_ust; most are named <X>_pinsight_lttng_ust
# (provider reported as "<X>"), but two are pinsight_<X>_lttng_ust
# (pinsight_enter_exit, pinsight_manifest — reported with that full prefix).
# The old pattern (\S+?)_pinsight_lttng_ust silently dropped those two.
_ev_re  = re.compile(r'(\S+?)_lttng_ust:(\w+):')
_host_re= re.compile(r'hostname = "([^"]*)"')
# generic "key = value" fields; values may be numbers, hex, quoted strings, or
# enum-like ( "label" : container = N ) blobs -- keep the raw string, callers
# int()/strip as needed.
_fld_re = re.compile(r'(\w+) = (0x[0-9A-Fa-f]+|-?\d+|"[^"]*"|\( "[^"]*" : container = \d+ \))')

@dataclass
class Event:
    t_ns: int
    host: str
    provider: str   # pmpi | roctracer | cupti | ompt | pysysmon | energy |
                    # pinsight_enter_exit | pinsight_manifest
    name: str       # e.g. MPI_Isend_begin, hipKernelActivity, manifest_kv
    fields: dict    # raw string values keyed by field name

    def i(self, key, default=None):
        v = self.fields.get(key)
        if v is None: return default
        try: return int(v, 0)
        except ValueError: return default

    def s(self, key, default=None):
        v = self.fields.get(key)
        return v.strip('"') if v is not None else default

def _to_ns(h, mi, s, f):
    return ((int(h)*3600+int(mi)*60+int(s))*1_000_000_000)+int(f)

def _events_one(trace_dirs, babeltrace="babeltrace2"):
    """Sequential path: one babeltrace2 over the given dirs (merged stream)."""
    proc = subprocess.Popen([babeltrace] + list(trace_dirs),
                            stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
                            text=True, bufsize=1<<20)
    try:
        for line in proc.stdout:
            tm = _ts_re.match(line)
            if not tm: continue
            em = _ev_re.search(line)
            if not em: continue
            hm = _host_re.search(line)
            prov = em.group(1)
            if prov.endswith("_pinsight"):
                prov = prov[:-9]   # pmpi_pinsight -> pmpi (etc.)
            yield Event(t_ns=_to_ns(*tm.groups()),
                        host=hm.group(1) if hm else "",
                        provider=prov, name=em.group(2),
                        fields=dict(_fld_re.findall(line)))
    finally:
        # No orphaned decoders: kill on abnormal exit (harmless after normal
        # completion). Workers convert SIGTERM to SystemExit so this runs.
        proc.kill()
        proc.wait()

# ---------------- parallel decode (default) --------------------------------
# babeltrace2 decode + regex parse dominate analysis wall time. Each trace dir
# is decoded/parsed in its OWN worker process (batched tuples over a bounded
# queue), and the parent heap-merges the per-dir streams by timestamp. Each
# single dir's stream is already time-ordered, so the merged stream is
# IDENTICAL to the sequential one — analyses need no changes and results are
# byte-identical by construction.
#   Worker count: PEAM_PAR env if set, else min(#dirs, cpu/2); <=1 dir or
#   PEAM_PAR=1 falls back to the sequential path.
_BATCH = 4096

def _decode_worker(dirs, babeltrace, q):
    import signal
    signal.signal(signal.SIGTERM, lambda *_: sys.exit(1))  # run finallys
    try:
        batch = []
        for ev in _events_one(dirs, babeltrace):
            batch.append((ev.t_ns, ev.host, ev.provider, ev.name, ev.fields))
            if len(batch) >= _BATCH:
                q.put(batch); batch = []
        if batch: q.put(batch)
    finally:
        q.put(None)                       # sentinel: this worker is done

def _queue_stream(q):
    while True:
        batch = q.get()
        if batch is None: return
        for t in batch:
            yield t

def events(trace_dirs, babeltrace="babeltrace2", parallel=None):
    """Yield Event for every PInsight event across the given trace dirs,
    time-ordered. Decodes dirs in parallel by default (see above);
    parallel=1 forces the sequential single-babeltrace2 path."""
    import os as _os, heapq
    dirs = list(trace_dirs)
    if parallel is None:
        parallel = int(_os.environ.get("PEAM_PAR", 0)) or \
                   max(1, min(len(dirs), (_os.cpu_count() or 8) // 2))
    if parallel <= 1 or len(dirs) <= 1:
        yield from _events_one(dirs, babeltrace)
        return
    import multiprocessing as mp
    ctx = mp.get_context("fork")
    n = min(parallel, len(dirs))
    groups = [dirs[i::n] for i in range(n)]   # round-robin; each worker's own
    qs, procs = [], []                        # babeltrace2 merge keeps its
    try:                                      # sub-stream time-ordered
        for g in groups:
            q = ctx.Queue(maxsize=8)      # bounded: backpressure, ~modest RAM
            p = ctx.Process(target=_decode_worker, args=(g, babeltrace, q),
                            daemon=True)
            p.start(); qs.append(q); procs.append(p)
        for t in heapq.merge(*(_queue_stream(q) for q in qs),
                             key=lambda t: t[0]):
            yield Event(*t)
    finally:
        for p in procs:
            if p.is_alive(): p.terminate()
            p.join()

class BeginEndMatcher:
    """Pair *_begin/*_end events per (rank) into durations.
    add() returns (base_name, duration_ns, begin_event) on each completed pair."""
    def __init__(self):
        self.open = {}
    def add(self, ev, rank):
        if ev.name.endswith("_begin"):
            self.open[(rank, ev.name[:-6])] = ev
            return None
        if ev.name.endswith("_end"):
            base = ev.name[:-4]
            b = self.open.pop((rank, base), None)
            if b is not None:
                return base, ev.t_ns - b.t_ns, b
        return None

def find_traces(path):
    """A CTF trace dir is one containing a `metadata` file. If `path` is
    itself a trace dir, return just it; otherwise return every trace dir
    beneath it (so a run folder expands to all of its per-node traces)."""
    import os
    if os.path.isfile(os.path.join(path, "metadata")):
        return [path]
    out = []
    for root, subdirs, files in os.walk(path):
        if "metadata" in files:
            out.append(root)
            subdirs[:] = []          # a trace dir has no nested traces
    return sorted(out)

def expand_dirs(paths):
    """Expand each argument to CTF trace dirs (see find_traces), warn about
    and skip non-directories, and deduplicate while preserving order. This is
    the standard argument semantic for all the analysis scripts: pass exact
    trace dirs, node folders, run folders, or any mix."""
    import os
    out = []
    for d in paths:
        if os.path.isdir(d):
            traces = find_traces(d)
            if traces:
                out.extend(traces)
            else:
                print(f"[pinsight] no CTF trace (metadata file) under '{d}'",
                      file=sys.stderr)
        else:
            print(f"[pinsight] skipping '{d}': not a directory",
                  file=sys.stderr)
    seen = set(); uniq = []
    for d in out:
        if d not in seen:
            seen.add(d); uniq.append(d)
    return uniq

# ---------------- manifest access (WS1 Step 5) ----------------------------
# The manifest makes traces self-describing (doc/user/manifest.md): each
# process periodically emits a BURST — manifest_process + N manifest_kv
# (key, value) events sharing `seq`. Consumption rule: LATEST-WINS PER KEY
# per (hostname, pid); bursts are idempotent and full (no deltas), so a
# window sliced mid-burst still resolves from the previous burst's keys.

def manifests(trace_dirs, at_ns=None, babeltrace="babeltrace2"):
    """Manifest facts per process: {(hostname, pid): {key: value}}.
    Latest-wins per key over the time-ordered stream; with at_ns, only
    bursts at or before that timestamp count ("what was true then" — e.g.
    pinsight.config_hash identifies the config epoch of a window).
    manifest_process fields appear as mpirank/exe/window_gen/nprocs_hint;
    kv values are plain strings. Missing manifests => {} (analyses must
    degrade gracefully — manifest facts are additive)."""
    out = {}
    for ev in events(trace_dirs, babeltrace):
        if ev.provider != "pinsight_manifest":
            continue
        if at_ns is not None and ev.t_ns > at_ns:
            continue
        d = out.setdefault((ev.host, ev.i("pid")), {})
        if ev.name == "manifest_process":
            for f in ("mpirank", "window_gen", "nprocs_hint"):
                v = ev.i(f)
                if v is not None:
                    d[f] = v
            v = ev.s("exe")
            if v:
                d["exe"] = v
        elif ev.name == "manifest_kv":
            k = ev.s("key")
            if k:
                d[k] = ev.s("value", "")
    return out

def load_run_manifest(path):
    """The run-level sidecar (run_manifest.json, written by
    scripts/pinsight-manifest.sh): given a run dir, a trace dir inside one,
    or the JSON path itself, ascend looking for it (a CTF dir sits at least
    4 levels — ust/uid/<uid>/64-bit — below its node dir, plus run layout).
    Returns the parsed dict, or None (traces never depend on the sidecar)."""
    import json, os
    p = os.path.abspath(path)
    if os.path.isfile(p) and os.path.basename(p) == "run_manifest.json":
        return json.load(open(p))
    for _ in range(9):
        cand = os.path.join(p, "run_manifest.json")
        if os.path.isfile(cand):
            return json.load(open(cand))
        parent = os.path.dirname(p)
        if parent == p:
            break
        p = parent
    return None

# ---------------- neutral machine-readable output (--json / --csv) --------
# Every analysis script declares TABLE SPECS: an ordered dict
#   {table_name: {"title": str, "columns": [(col_name, col_type), ...]}}
# with col_type in: int | string | number | duration_s | ratio | bytes.
# Rows carry PLAIN values in natural units (seconds as float, bytes as int,
# ratio as 0..1 float). Tool adapters (e.g. tc/lami_adapter.py) convert from
# this one contract; --json/--csv emit it directly.

def emit_json(analysis, specs, tables_rows, span):
    import json
    out = {"analysis": analysis, "span_ns": list(span), "tables": []}
    for name, spec in specs.items():
        out["tables"].append({
            "name": name, "title": spec["title"],
            "columns": [{"name": n, "type": t} for n, t in spec["columns"]],
            "rows": tables_rows.get(name, [])})
    print(json.dumps(out, indent=2))

def emit_csv(specs, tables_rows):
    import csv
    w = csv.writer(sys.stdout)
    multi = len(specs) > 1
    for name, spec in specs.items():
        if multi: print(f"# table: {name}")
        w.writerow([n for n, _ in spec["columns"]])
        for r in tables_rows.get(name, []):
            w.writerow(r)
        if multi: print()

def cli_main(argv, analysis, specs, build_tables, text_fn, usage):
    """Standard CLI for the analysis scripts: paths (dirs expand to all CTF
    traces beneath), --json, --csv; default = human-readable text.
    build_tables(dirs) -> ({table_name: rows}, (t0_ns, t1_ns))."""
    mode = "text"; paths = []
    for a in argv:
        if a == "--json":   mode = "json"
        elif a == "--csv":  mode = "csv"
        elif a.startswith("--"):
            print(f"unknown option {a}\n{usage}", file=sys.stderr); return 1
        else:
            paths.append(a)
    if not paths:
        print(usage); return 1
    dirs = expand_dirs(paths)
    if not dirs:
        return 1
    if mode == "text":
        text_fn(dirs); return 0
    tables_rows, span = build_tables(dirs)
    if mode == "json":
        emit_json(analysis, specs, tables_rows, span)
    else:
        emit_csv(specs, tables_rows)
    return 0

def fmt_bytes(n):
    for unit in ("B","KB","MB","GB","TB"):
        if n < 1024 or unit == "TB": return f"{n:.1f} {unit}" if unit!="B" else f"{n} B"
        n /= 1024

def percentile(sorted_vals, p):
    if not sorted_vals: return 0
    k = (len(sorted_vals)-1) * p / 100
    lo = int(k)
    hi = min(lo+1, len(sorted_vals)-1)
    return sorted_vals[lo] + (sorted_vals[hi]-sorted_vals[lo]) * (k-lo)
