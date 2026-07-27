#!/usr/bin/env python3
# Load-imbalance analysis across ALL ranks of a (multi-node) MPI+HIP PInsight
# trace: how much of each rank's wall time is real work vs waiting on others.
#
#   usage: python3 load_imbalance.py <trace_dir> [<trace_dir> ...]
#          (pass every node's trace dir of one run; they are time-merged)
#
# Per rank: wall time, blocking-MPI ("wait") time, non-blocking-posting time,
# GPU-sync time, kernel-launch + memcpy host overheads. The imbalance signature
# is a rank with low wait and high everything-else (the straggler everyone
# waits FOR) vs ranks with high wait (the ones waiting).
#
# TraceCompass integration lives in tc/ (adapters + wrapper), NOT here — this
# script only processes LTTng/CTF trace data and knows nothing about TC.
import sys
from collections import defaultdict
from pinsight_reader import events, BeginEndMatcher

MPI_WAIT = {"MPI_Waitall","MPI_Wait","MPI_Allreduce","MPI_Barrier","MPI_Allgather",
            "MPI_Recv","MPI_Send"}   # blocking calls: time here = waiting/synchronizing

def analyze(dirs, begin_tod_ns=None, end_tod_ns=None):
    m = BeginEndMatcher()
    dur   = defaultdict(lambda: defaultdict(float))  # [rank][cat] s
    first = {}; last = {}; host_of = {}
    for ev in events(dirs):
        if begin_tod_ns is not None and ev.t_ns < begin_tod_ns: continue
        if end_tod_ns is not None and ev.t_ns > end_tod_ns: continue
        r = ev.i("mpirank")
        if r is None: continue
        if ev.provider in ("pmpi","roctracer","pinsight_enter_exit"):
            if r not in first: first[r] = ev.t_ns
            last[r] = ev.t_ns
            if ev.host: host_of[r] = ev.host
        if ev.provider not in ("pmpi","roctracer"): continue
        got = m.add(ev, r)
        if not got: continue
        base, dns, _b = got
        d = dns/1e9
        if ev.provider == "pmpi":
            dur[r]["mpi_total"] += d
            dur[r]["mpi_wait" if base in MPI_WAIT else "mpi_post"] += d
        elif base in ("hipStreamSync","hipDeviceSync"):
            dur[r]["gpu_sync"] += d
        elif base == "hipKernelLaunch":
            dur[r]["launch"] += d
        elif base in ("hipMemcpy","hipMemcpyAsync"):
            dur[r]["memcpy"] += d

    rows = []
    for r in sorted(first):
        wall = (last[r]-first[r])/1e9
        d = dur[r]
        acct = d["mpi_total"]+d["gpu_sync"]+d["launch"]+d["memcpy"]
        rows.append(dict(rank=r, host=host_of.get(r,"?"), wall=wall,
                         mpi_wait=d["mpi_wait"], mpi_post=d["mpi_post"],
                         gpu_sync=d["gpu_sync"], launch=d["launch"],
                         memcpy=d["memcpy"], other=max(wall-acct,0),
                         wait_ratio=(d["mpi_wait"]+d["gpu_sync"])/wall if wall else 0))
    span = (min(first.values()), max(last.values())) if first else (0, 0)
    return rows, span

def print_text(rows):
    print(f"{'rank':>4} {'host':>14} {'wall(s)':>8} {'MPIwait':>8} {'MPIpost':>8} "
          f"{'GPUsync':>8} {'launch':>7} {'memcpy':>7} {'other':>8}  wait%")
    print("-"*88)
    for x in rows:
        print(f"{x['rank']:>4} {x['host']:>14} {x['wall']:8.2f} {x['mpi_wait']:8.3f} "
              f"{x['mpi_post']:8.3f} {x['gpu_sync']:8.3f} {x['launch']:7.3f} "
              f"{x['memcpy']:7.3f} {x['other']:8.2f}  {100*x['wait_ratio']:5.1f}")
    print("-"*88)
    waits = [x["mpi_wait"] for x in rows]
    if waits and sum(waits):
        mean = sum(waits)/len(waits)
        print(f"MPI-wait imbalance (max-min)/mean: {(max(waits)-min(waits))/mean*100:.0f}%   "
              f"min={min(waits):.2f}s max={max(waits):.2f}s")
        least = min(rows, key=lambda x: x["mpi_wait"])
        print(f"least-waiting rank (likely straggler others wait for): "
              f"rank {least['rank']} on {least['host']}")

# neutral table contract (see pinsight_reader.py: --json/--csv + adapters)
TITLE = "PInsight: per-rank load imbalance"
DESCRIPTION = "Per-rank wall/wait/work breakdown from a PInsight MPI+HIP trace"
TABLE_SPECS = {
    "per_rank_load": {"title": "Per-rank load imbalance", "columns": [
        ("rank","int"), ("host","string"), ("wall_s","duration_s"),
        ("mpi_wait_s","duration_s"), ("mpi_post_s","duration_s"),
        ("gpu_sync_s","duration_s"), ("launch_s","duration_s"),
        ("memcpy_s","duration_s"), ("other_s","duration_s"),
        ("wait_ratio","ratio")]},
}

def build_tables(dirs, b_tod=None, e_tod=None):
    rows, span = analyze(dirs, b_tod, e_tod)
    data = [[x["rank"], x["host"], x["wall"], x["mpi_wait"], x["mpi_post"],
             x["gpu_sync"], x["launch"], x["memcpy"], x["other"],
             x["wait_ratio"]] for x in rows]
    return {"per_rank_load": data}, span

if __name__ == "__main__":
    from pinsight_reader import cli_main
    sys.exit(cli_main(sys.argv[1:], "load_imbalance", TABLE_SPECS, build_tables,
                      lambda dirs: print_text(analyze(dirs)[0]),
                      "usage: load_imbalance.py [--json|--csv] <trace_or_folder>..."))
