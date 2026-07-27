#!/usr/bin/env python3
# MPI blocking-wait jitter analysis — app-agnostic.
#
# For every paired MPI_*_begin/_end call type in the trace, per rank, blocking
# durations are computed from single-rank own-clock deltas (NO cross-node clock
# alignment needed). Call types are ranked by total wait; for the top ones:
#   - per-rank TOTAL wait  -> cross-rank spread (CV, max/min): straggler signal
#   - individual durations -> p50 / p99 and tail ratio p99/p50: jitter signal
#
# The "amplified-jitter signature" (vs a baseline run of the same app/scale):
# the dominant collective's per-call MEDIAN inflates several-fold while the
# cross-rank CV COLLAPSES (everyone waits equally on rotating stragglers) and
# neighbor waits stay flat. This indicates small per-rank perturbations being
# amplified through tight global collectives — the collective completes only
# when the slowest rank arrives. Apps without frequent global sync express
# jitter as a fat p99 tail instead (no median inflation).
#
#   usage: mpi_jitter.py [options] [--json|--csv] <trace_or_folder>...
#     <trace_or_folder>...   traces of ONE run (peam convention; node dirs merge)
#     --baseline DIR[,DIR..] second run to compare against (deltas + verdict)
#     --calls A,B,..         analyze only these MPI calls (default: auto-detect,
#                            ranked by total wait)
#     --top N                how many auto-detected calls to report (default 6)
#   Decode is parallel by default (pinsight_reader.events); PEAM_PAR=1 forces
#   sequential.
import sys, os, statistics
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pinsight_reader import (events, BeginEndMatcher, expand_dirs, percentile,
                             emit_json, emit_csv)

EXCLUDE = {"MPI_Init", "MPI_Init_thread", "MPI_Finalize"}  # whole-run spans

def collect(dirs):
    """{call: {rank: [dur_us,...]}} for all paired MPI calls + (t0,t1) span."""
    m = BeginEndMatcher()
    per, t0, t1 = {}, None, None
    for ev in events(dirs):
        if ev.provider != "pmpi": continue
        t0 = ev.t_ns if t0 is None else t0
        t1 = ev.t_ns
        r = ev.i("mpirank")
        if r is None: continue
        got = m.add(ev, r)
        if not got: continue
        base, dns, _b = got
        if base in EXCLUDE: continue
        per.setdefault(base, {}).setdefault(r, []).append(dns / 1e3)
    return per, (t0 or 0, t1 or 0)

def stats(per_rank):
    ranks = sorted(per_rank)
    totals = [sum(per_rank[r]) / 1e6 for r in ranks]          # seconds
    alldur = sorted(d for r in ranks for d in per_rank[r])
    mean_t = statistics.mean(totals)
    sd_t   = statistics.pstdev(totals) if len(totals) > 1 else 0.0
    p50, p99 = percentile(alldur, 50), percentile(alldur, 99)
    return dict(nranks=len(ranks), ncalls=len(alldur), mean_wait_s=mean_t,
                cv=(sd_t / mean_t if mean_t else 0.0),
                min_s=min(totals), max_s=max(totals),
                p50_us=p50, p99_us=p99,
                tail=(p99 / p50 if p50 else 0.0))

def top_calls(per, calls, topn):
    if calls:
        return [c for c in calls if c in per]
    ranked = sorted(per, key=lambda c: -sum(sum(v) for v in per[c].values()))
    return ranked[:topn]

def block(call, k):
    return (f"  {call:>18}: ranks={k['nranks']:>3}  calls={k['ncalls']:>8}\n"
            f"    per-rank TOTAL wait (s): mean={k['mean_wait_s']:8.3f}  "
            f"CV={k['cv']:6.3f}  min={k['min_s']:7.3f}  max={k['max_s']:7.3f}\n"
            f"    individual dur (us):     p50={k['p50_us']:8.2f}  "
            f"p99={k['p99_us']:10.2f}  tail p99/p50={k['tail']:8.1f}")

def verdict(kb, ks):
    """Baseline vs suspect stats of the dominant call -> signature verdict."""
    if not kb or not ks or not kb["p50_us"]: return None
    infl = ks["p50_us"] / kb["p50_us"]
    dcv  = ks["cv"] - kb["cv"]
    hit  = infl >= 3.0 and dcv <= -0.05
    return infl, dcv, hit

TABLE_SPECS = {"mpi_jitter": {"title": "MPI blocking-wait jitter by call type",
    "columns": [("call", "string"), ("nranks", "int"), ("ncalls", "int"),
                ("mean_wait_s", "duration_s"), ("cv", "ratio"),
                ("p50_us", "number"), ("p99_us", "number"), ("tail", "number")]}}

def main(argv):
    mode, paths, base_paths, calls, topn = "text", [], [], None, 6
    it = iter(argv)
    for a in it:
        if a == "--json": mode = "json"
        elif a == "--csv": mode = "csv"
        elif a == "--baseline": base_paths = next(it, "").split(",")
        elif a == "--calls": calls = next(it, "").split(",")
        elif a == "--top": topn = int(next(it, "6"))
        elif a.startswith("--"):
            print(f"unknown option {a}", file=sys.stderr); return 1
        else: paths.append(a)
    if not paths:
        print(__doc__ or "usage: mpi_jitter.py [options] <trace_or_folder>...")
        print("usage: mpi_jitter.py [--baseline DIR] [--calls A,B] [--top N] "
              "[--json|--csv] <trace_or_folder>...")
        return 1
    dirs = expand_dirs(paths)
    if not dirs: return 1
    per, span = collect(dirs)
    if not per:
        print("no paired pmpi events found"); return 1
    sel = top_calls(per, calls, topn)
    ks = {c: stats(per[c]) for c in sel}

    kb = {}
    if base_paths:
        bdirs = expand_dirs([p for p in base_paths if p])
        if bdirs:
            bper, _ = collect(bdirs)
            kb = {c: stats(bper[c]) for c in sel if c in bper}

    if mode in ("json", "csv"):
        rows = [[c, k["nranks"], k["ncalls"], k["mean_wait_s"], k["cv"],
                 k["p50_us"], k["p99_us"], k["tail"]] for c, k in ks.items()]
        if mode == "json":
            emit_json("mpi_jitter", TABLE_SPECS, {"mpi_jitter": rows}, span)
        else:
            emit_csv(TABLE_SPECS, {"mpi_jitter": rows})
        return 0

    print(f"\n== MPI blocking-wait jitter ({len(dirs)} node traces; "
          f"calls ranked by total wait) ==")
    for c in sel:
        print(block(c, ks[c]))
        if c in kb:
            b = kb[c]
            print(f"    vs baseline:             p50 x{ks[c]['p50_us']/b['p50_us']:.1f}"
                  f"  CV {b['cv']:.3f}->{ks[c]['cv']:.3f}"
                  f"  meanWait {b['mean_wait_s']:.3f}->{ks[c]['mean_wait_s']:.3f}s"
                  if b["p50_us"] else "    vs baseline: (no events)")
    if kb and sel:
        hits, checked = [], []
        for c in sel:
            v = verdict(kb.get(c), ks.get(c))
            if not v: continue
            infl, dcv, hit = v
            checked.append((c, infl, dcv))
            if hit: hits.append((c, infl, dcv))
        if hits:
            for c, infl, dcv in hits:
                print(f"\n  amplified-jitter signature: DETECTED in {c} "
                      f"(p50 x{infl:.1f}, dCV {dcv:+.3f})")
        elif checked:
            print(f"\n  amplified-jitter signature: absent in all "
                  f"{len(checked)} compared calls "
                  f"(criteria: p50 x>=3.0 and dCV<=-0.05)")
    return 0

if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
