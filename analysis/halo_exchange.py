#!/usr/bin/env python3
# Halo-exchange vs collective cost analysis from a (multi-node) PInsight trace.
#
#   usage: python3 halo_exchange.py <trace_dir> [<trace_dir> ...]
#
# Splits MPI cost into the two structurally different patterns:
#   halo (point-to-point): Isend/Irecv posting + the Wait/Waitall completion
#       time that pays for the actual transfers
#   collectives: Allreduce/Barrier/Allgather -- global synchronization
# and reports the per-rank neighbor topology (who exchanges with whom, how
# much, learned from dest/source fields) plus a message-size profile per
# neighbor-distance class (same-node peer vs cross-node peer).
#
# Multigrid caveat baked into the size profile: coarse levels send many TINY
# messages (latency-bound) while fine levels send few LARGE ones
# (bandwidth-bound) -- if small-message count dominates while byte volume is
# dominated by large ones, coarse-level latency is the thing to optimize
# (e.g. via aggregation), not bandwidth. count = MPI elements (datatype not
# traced; for AMG doubles multiply by 8 for approx bytes).
import sys
from collections import defaultdict
from pinsight_reader import events, BeginEndMatcher, percentile

POST = {"MPI_Isend":"dest","MPI_Irecv":"source","MPI_Send":"dest","MPI_Recv":"source"}
WAIT = {"MPI_Wait","MPI_Waitall"}
COLL = {"MPI_Allreduce","MPI_Barrier","MPI_Allgather"}

def collect(dirs, b_tod=None, e_tod=None):
    m = BeginEndMatcher()
    host_of = {}
    t_post = defaultdict(float); t_wait = defaultdict(float); t_coll = defaultdict(float)
    n_coll = defaultdict(lambda: defaultdict(int))
    peer_msgs  = defaultdict(int)      # (rank, peer) -> messages
    peer_count = defaultdict(int)      # (rank, peer) -> total MPI-element count
    sizes = []                          # per-message element counts (halo only)
    t0 = None; t1 = 0

    for ev in events(dirs):
        if b_tod is not None and ev.t_ns < b_tod: continue
        if e_tod is not None and ev.t_ns > e_tod: continue
        r = ev.i("mpirank")
        if r is None or ev.provider != "pmpi": continue
        if t0 is None: t0 = ev.t_ns
        t1 = ev.t_ns
        if ev.host: host_of[r] = ev.host
        got = m.add(ev, r)
        if not got: continue
        base, dns, b = got
        s = dns/1e9
        if base in POST:
            t_post[r] += s
            peer = b.i(POST[base])
            if peer is not None:
                peer_msgs[(r,peer)] += 1
                peer_count[(r,peer)] += b.i("count",0)
            c = b.i("count")
            if c is not None: sizes.append(c)
        elif base in WAIT:
            t_wait[r] += s
        elif base in COLL:
            t_coll[r] += s
            n_coll[r][base] += 1

    return (host_of, t_post, t_wait, t_coll, peer_msgs, peer_count, sizes,
            (t0 or 0, t1))

def main(dirs):
    (host_of, t_post, t_wait, t_coll, peer_msgs, peer_count, sizes,
     _span) = collect(dirs)
    ranks = sorted(set(t_post)|set(t_wait)|set(t_coll))
    print("== halo (P2P) vs collective time per rank (s) ==")
    print(f"{'rank':>4} {'host':>14} {'p2p post':>9} {'wait*':>9} {'halo=p+w':>9} "
          f"{'collect':>9} {'#neigh':>6}")
    print("-"*66)
    for r in ranks:
        nb = len({p for (rr,p) in peer_msgs if rr==r})
        print(f"{r:>4} {host_of.get(r,'?'):>14} {t_post[r]:>9.3f} {t_wait[r]:>9.3f} "
              f"{t_post[r]+t_wait[r]:>9.3f} {t_coll[r]:>9.3f} {nb:>6}")
    tot_halo = sum(t_post.values())+sum(t_wait.values())
    tot_coll = sum(t_coll.values())
    print("-"*66)
    print(f"all-rank totals: halo {tot_halo:.2f}s   collectives {tot_coll:.2f}s   "
          f"ratio halo/coll = {tot_halo/tot_coll if tot_coll else float('inf'):.1f}x")

    print("\n== heaviest neighbor pairs by message count ==")
    print(f"{'rank':>4} -> {'peer':>4} {'link':>10} {'msgs':>8} {'elements':>12}")
    top = sorted(peer_msgs.items(), key=lambda kv: -kv[1])[:16]
    for (r,p), n in top:
        h1,h2 = host_of.get(r), host_of.get(p)
        link = "same-node" if h1 and h1==h2 else "cross-node" if h1 and h2 else "?"
        print(f"{r:>4} -> {p:>4} {link:>10} {n:>8} {peer_count[(r,p)]:>12}")

    if sizes:
        v = sorted(sizes)
        small = sum(1 for c in v if c <= 64)
        vol_small = sum(c for c in v if c <= 64)
        print(f"\n== halo message-size profile (elements/message) ==")
        print(f"n={len(v)}  p50={percentile(v,50):.0f}  p95={percentile(v,95):.0f}  "
              f"max={v[-1]}")
        print(f"messages <=64 elements: {small} ({100*small/len(v):.0f}% of messages, "
              f"{100*vol_small/sum(v):.1f}% of volume)"
              f"  -> {'LATENCY-bound tail dominates' if small/len(v)>0.5 else 'bandwidth-dominated'}")

# neutral table contract (see pinsight_reader.py: --json/--csv + adapters)
TITLE = "PInsight: halo exchange vs collectives"
DESCRIPTION = ("Per-rank halo (P2P post+wait) vs collective MPI cost, and "
               "heaviest neighbor pairs")
TABLE_SPECS = {
    "halo_per_rank": {"title": "Halo vs collective time per rank", "columns": [
        ("rank","int"), ("host","string"), ("p2p_post_s","duration_s"),
        ("wait_s","duration_s"), ("halo_s","duration_s"),
        ("collectives_s","duration_s"), ("neighbors","int")]},
    "halo_pairs": {"title": "Heaviest neighbor pairs", "columns": [
        ("rank","int"), ("peer","int"), ("link","string"),
        ("messages","int"), ("elements","int")]},
}

def build_tables(dirs, b_tod=None, e_tod=None):
    (host_of, t_post, t_wait, t_coll, peer_msgs, peer_count, sizes,
     span) = collect(dirs, b_tod, e_tod)
    ranks = sorted(set(t_post)|set(t_wait)|set(t_coll))
    rrows = []
    for r in ranks:
        nb = len({p for (rr,p) in peer_msgs if rr==r})
        rrows.append([r, host_of.get(r,"?"), t_post[r], t_wait[r],
                      t_post[r]+t_wait[r], t_coll[r], nb])
    prows = []
    for (r,p), n in sorted(peer_msgs.items(), key=lambda kv: -kv[1])[:16]:
        h1,h2 = host_of.get(r), host_of.get(p)
        link = "same-node" if h1 and h1==h2 else "cross-node" if h1 and h2 else "?"
        prows.append([r, p, link, n, peer_count[(r,p)]])
    return {"halo_per_rank": rrows, "halo_pairs": prows}, span

if __name__ == "__main__":
    from pinsight_reader import cli_main
    sys.exit(cli_main(sys.argv[1:], "halo_exchange", TABLE_SPECS, build_tables,
                      main,
                      "usage: halo_exchange.py [--json|--csv] <trace_or_folder>..."))
