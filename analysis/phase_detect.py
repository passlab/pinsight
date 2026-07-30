#!/usr/bin/env python3
# Phase detection for PInsight traces -- app-agnostic.
#
# Identifies execution phases (init / setup / iterative solve / ...) from the
# trace alone, without visualization or application knowledge, and detects the
# iteration period inside each phase:
#   1. Bin the event stream into fixed time slices; per bin compute a feature
#      vector: MPI call rate, in-MPI time fraction, collective share, kernel
#      launch rate, GPU activity time, memcpy count.
#   2. Change-point segmentation (binary segmentation, variance cost) over the
#      z-normalized multivariate series -> phase boundaries.
#   3. Autocorrelation of the MPI-call-rate series within each phase ->
#      iteration period and cycle count (iterative phases score high).
#
#   usage: python3 phase_detect.py [--json|--csv] <trace_or_folder>...
#          (pass every node's trace dir of one run; they are time-merged)
#
# TraceCompass integration lives in tc/ (adapters + wrapper), NOT here.
import sys
from collections import defaultdict
from pinsight_reader import events, expand_dirs

BASE_MS = 10              # collection resolution (periodicity uses this)
TARGET_BINS = 500         # segmentation runs on ~this many aggregated bins
MIN_SEG_BINS = 8          # smallest phase = 8 aggregated bins
MAX_PHASES = 10
GAIN_FRAC = 0.04          # min variance-reduction fraction to accept a split

COLLECTIVE_HINTS = ("Allreduce", "Reduce", "Bcast", "Barrier", "Allgather",
                    "Alltoall", "Scatter", "Gather", "Scan")
FEATS = ("mpi_rate", "mpi_frac", "coll_share", "kern_rate", "act_frac",
         "memcpy_rate", "par_rate", "omp_sync_frac")

def collect(dirs, bin_ms=BASE_MS):
    bin_ns = bin_ms * 1_000_000
    bins = defaultdict(lambda: defaultdict(float))
    open_mpi = {}
    t0 = None; t1 = 0
    for ev in events(dirs):
        if t0 is None: t0 = ev.t_ns
        t1 = ev.t_ns
        b = (ev.t_ns - t0) // bin_ns
        if ev.provider == "pmpi":
            if ev.name.endswith("_begin"):
                r = ev.i("mpirank")
                base = ev.name[:-6]
                bins[b]["mpi_calls"] += 1
                if any(h in base for h in COLLECTIVE_HINTS):
                    bins[b]["coll_calls"] += 1
                open_mpi[(r, base)] = (ev.t_ns, b)
            elif ev.name.endswith("_end"):
                k = (ev.i("mpirank"), ev.name[:-4])
                st = open_mpi.pop(k, None)
                if st: bins[st[1]]["mpi_time"] += ev.t_ns - st[0]
        elif ev.provider in ("roctracer", "cupti"):
            if ev.name in ("hipKernelLaunch_begin", "cudaKernelLaunch_begin"):
                bins[b]["kern_launch"] += 1
            elif ev.name in ("hipKernelActivity", "cudaKernelActivity"):
                d = (ev.i("end_ns") or 0) - (ev.i("begin_ns") or 0)
                if d > 0: bins[b]["act_time"] += d
            elif "Memcpy" in ev.name and ev.name.endswith("_begin"):
                bins[b]["memcpy"] += 1
        elif ev.provider == "ompt":
            if ev.name == "parallel_begin":
                bins[b]["par"] += 1
            elif ev.name.endswith("sync_wait_begin"):
                open_mpi[("omp", ev.i("global_thread_num"), ev.name[:-6])] = (ev.t_ns, b)
            elif ev.name.endswith("sync_wait_end"):
                st = open_mpi.pop(("omp", ev.i("global_thread_num"), ev.name[:-4]), None)
                if st: bins[st[1]]["omp_sync"] += ev.t_ns - st[0]
    if t0 is None: return [], 0, (0, 0), bin_ms
    nb = int((t1 - t0) // bin_ns) + 1
    rows = []
    for i in range(nb):
        d = bins.get(i, {})
        sec = bin_ns / 1e9
        rows.append({
            "mpi_rate": d.get("mpi_calls", 0) / sec,
            "mpi_frac": min(d.get("mpi_time", 0) / bin_ns, 4.0),  # summed over ranks
            "coll_share": d.get("coll_calls", 0) / d.get("mpi_calls", 1) if d.get("mpi_calls") else 0.0,
            "kern_rate": d.get("kern_launch", 0) / sec,
            "act_frac": min(d.get("act_time", 0) / bin_ns, 8.0),
            "memcpy_rate": d.get("memcpy", 0) / sec,
            "par_rate": d.get("par", 0) / sec,
            "omp_sync_frac": min(d.get("omp_sync", 0) / bin_ns, 128.0)})
    return rows, nb, (t0, t1), bin_ms

def _znorm(series):
    n = len(series)
    cols = []
    for f in FEATS:
        v = [row[f] for row in series]
        mu = sum(v) / n
        sd = (sum((x - mu) ** 2 for x in v) / n) ** 0.5 or 1.0
        cols.append([(x - mu) / sd for x in v])
    return list(zip(*cols))   # per-bin z-vector tuples

def _cost(z, a, b):
    """Within-segment variance cost of z[a:b] (sum over features)."""
    n = b - a
    if n <= 1: return 0.0
    c = 0.0
    for f in range(len(FEATS)):
        s = sum(z[i][f] for i in range(a, b))
        s2 = sum(z[i][f] ** 2 for i in range(a, b))
        c += s2 - s * s / n
    return c

def segment(z):
    """Binary segmentation: recursively split while variance gain is large."""
    total = _cost(z, 0, len(z))
    segs = [(0, len(z))]
    while len(segs) < MAX_PHASES:
        best = None
        for si, (a, b) in enumerate(segs):
            if b - a < 2 * MIN_SEG_BINS: continue
            base = _cost(z, a, b)
            for cut in range(a + MIN_SEG_BINS, b - MIN_SEG_BINS):
                gain = base - _cost(z, a, cut) - _cost(z, cut, b)
                if best is None or gain > best[0]:
                    best = (gain, si, cut)
        if best is None or best[0] < GAIN_FRAC * max(total, 1e-12):
            break
        _, si, cut = best
        a, b = segs[si]
        segs[si:si + 1] = [(a, cut), (cut, b)]
    return sorted(segs)

def periodicity(series, a, b, bin_ms):
    """Autocorrelation peak of mpi_rate in bins [a,b) -> (period_s, cycles, r)."""
    v = [series[i]["mpi_rate"] + series[i]["par_rate"] for i in range(a, b)]
    n = len(v)
    if n < 12: return None
    mu = sum(v) / n
    v = [x - mu for x in v]
    denom = sum(x * x for x in v) or 1.0
    best = None
    for lag in range(2, n // 3 + 1):
        r = sum(v[i] * v[i + lag] for i in range(n - lag)) / denom
        if best is None or r > best[1]:
            # prefer the first strong peak: accept only if clearly better
            if best is None or r > best[1] + 0.02:
                best = (lag, r)
    if not best or best[1] < 0.25: return None
    period_s = best[0] * bin_ms / 1000.0
    return period_s, (n * bin_ms / 1000.0) / period_s, best[1]

def characterize(series, a, b, run_mean):
    seg = {f: sum(series[i][f] for i in range(a, b)) / (b - a) for f in FEATS}
    tags = []
    if seg["mpi_rate"] > 2 * run_mean["mpi_rate"]: tags.append("MPI-dense")
    elif seg["mpi_rate"] < 0.5 * run_mean["mpi_rate"]: tags.append("MPI-sparse")
    if seg["coll_share"] > 0.5: tags.append("collective-heavy")
    elif seg["coll_share"] < 0.2 and seg["mpi_rate"] > 0: tags.append("p2p-heavy")
    if seg["kern_rate"] > 2 * run_mean["kern_rate"]: tags.append("kernel-dense")
    if seg["memcpy_rate"] > 2 * run_mean["memcpy_rate"]: tags.append("memcpy-heavy")
    if seg["par_rate"] > 2 * run_mean["par_rate"]: tags.append("OpenMP-dense")
    if seg["omp_sync_frac"] > 2 * run_mean["omp_sync_frac"] and seg["omp_sync_frac"] > 0.1:
        tags.append("OpenMP-sync-heavy")
    if not tags:
        tags.append("OpenMP-parallel" if seg["par_rate"] > 50 else "compute-dominated")
    return seg, "+".join(tags)

def _aggregate(series, k):
    """Average groups of k base bins into one coarse bin."""
    out = []
    for i in range(0, len(series) - k + 1, k):
        out.append({f: sum(series[j][f] for j in range(i, i + k)) / k
                    for f in FEATS})
    return out

def analyze(dirs, bin_ms=BASE_MS):
    series, nb, (t0, t1), bin_ms = collect(dirs, bin_ms)
    if not series: return [], (0, 0)
    # segment on ~TARGET_BINS coarse bins; keep the fine series for periodicity
    k = max(1, round(nb / TARGET_BINS))
    coarse = _aggregate(series, k)
    cms = bin_ms * k
    run_mean = {f: sum(r[f] for r in series) / nb for f in FEATS}
    segs = segment(_znorm(coarse))
    out = []
    for ca, cb in segs:
        a, b = ca * k, min(cb * k, nb)     # map back to fine-bin indices
        seg, tag = characterize(series, a, b, run_mean)
        per = periodicity(series, a, b, bin_ms)
        out.append({
            "start_s": a * bin_ms / 1000.0, "end_s": b * bin_ms / 1000.0,
            "dur_s": (b - a) * bin_ms / 1000.0, "character": tag,
            "mpi_rate": seg["mpi_rate"], "coll_share": seg["coll_share"],
            "kern_rate": seg["kern_rate"], "par_rate": seg["par_rate"],
            "omp_sync_frac": seg["omp_sync_frac"],
            "period_s": per[0] if per else None,
            "cycles": per[1] if per else None,
            "autocorr": per[2] if per else None})
    return out, (t0, t1)

def main(dirs):
    phases, _span = analyze(dirs)
    print(f"{'#':>2} {'start':>8} {'end':>8} {'dur s':>7} {'MPI/s':>8} "
          f"{'coll%':>6} {'kern/s':>8} {'par/s':>8} {'sync':>6} "
          f"{'period':>7} {'cycles':>7}  character")
    for i, p in enumerate(phases):
        per = f"{p['period_s']:.2f}s" if p["period_s"] else "-"
        cyc = f"{p['cycles']:.0f}" if p["cycles"] else "-"
        print(f"{i:>2} {p['start_s']:>7.1f}s {p['end_s']:>7.1f}s {p['dur_s']:>7.1f} "
              f"{p['mpi_rate']:>8.0f} {100*p['coll_share']:>5.0f}% "
              f"{p['kern_rate']:>8.0f} {p['par_rate']:>8.0f} "
              f"{p['omp_sync_frac']:>6.2f} {per:>7} {cyc:>7}  {p['character']}")

# neutral table contract (see pinsight_reader.py: --json/--csv + adapters)
TITLE = "PInsight: execution phase detection"
DESCRIPTION = ("Unsupervised phase segmentation from event-stream features, "
               "with per-phase iteration-period detection")
TABLE_SPECS = {
    "phases": {"title": "Detected execution phases", "columns": [
        ("phase", "int"), ("start_s", "duration_s"), ("end_s", "duration_s"),
        ("dur_s", "duration_s"), ("mpi_calls_per_s", "number"),
        ("collective_share", "ratio"), ("kernels_per_s", "number"),
        ("parallel_regions_per_s", "number"), ("omp_sync_threads", "number"),
        ("period_s", "duration_s"), ("cycles", "number"),
        ("character", "string")]},
}

def build_tables(dirs, b_tod=None, e_tod=None):
    phases, span = analyze(dirs)
    rows = [[i, p["start_s"], p["end_s"], p["dur_s"], p["mpi_rate"],
             p["coll_share"], p["kern_rate"], p["par_rate"],
             p["omp_sync_frac"], p["period_s"] or 0.0,
             p["cycles"] or 0.0, p["character"]]
            for i, p in enumerate(phases)]
    return {"phases": rows}, span

if __name__ == "__main__":
    from pinsight_reader import cli_main
    sys.exit(cli_main(sys.argv[1:], "phase_detect", TABLE_SPECS, build_tables,
                      main,
                      "usage: phase_detect.py [--json|--csv] <trace_or_folder>..."))
