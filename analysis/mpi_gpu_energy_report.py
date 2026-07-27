#!/usr/bin/env python3
# Per-rank load-balance, GPU-utilization, and energy report from a PInsight trace
# of any MPI + HIP (+ energy) application. Generalized 2026-07-17 from an
# AMG2023-specific script of the same shape (peam/src/python/amg2023/ had the
# app-specific predecessor); no app-specific assumptions should live here —
# app-specific parsing/labels belong in a thin wrapper or a separate script.
#
#   usage: babeltrace2 <trace> | python3 mpi_gpu_energy_report.py [--outdir figures] [--tag run1]
#
# What it reports, per rank: wall time, time in MPI (total + the synchronizing/
# blocking subset), time blocked on GPU sync, kernel-launch overhead, memcpy
# overhead, host-side load imbalance -- plus, node-wide: per-device GPU kernel
# execution time (from activity records), per-device energy (from the energy
# domain), and GPU kernel hotspots by (demangled) name.
#
# Per-rank GPU attribution
# ------------------------
# Host-side events (hipKernelLaunch/Sync, MPI_*) carry `mpirank` directly.
# Activity records (hipKernelActivity/hipMemcpyActivity) carry the *physical*
# `devId` instead (see PInsight's ROCR_VISIBLE_DEVICES-based devId fix,
# src/roctracer_callback.c) -- there is no rank field on activity records, so
# attributing GPU execution time to a rank requires a rank<->device mapping.
# This script infers that mapping ONLY when it is unambiguous: exactly one
# physical device seen per rank (the common one-GPU-per-rank deployment
# pattern). If that does not hold (fewer/more devices than ranks -- e.g.
# multiple ranks sharing a GPU, or a CPU-only run), per-rank GPU-exec
# attribution is skipped (reported as 0 with a note), rather than silently
# guessing wrong. Node-wide GPU stats (kernel hotspots, per-device energy)
# are always reported regardless, since those do not need the mapping.
#
# Energy note: gpu_uj is a NODE-WIDE counter -- every rank on a node reports
# the same values (see energy_lttng_ust_tracepoint.h). This script uses the
# earliest energy_enter and latest energy_exit across all ranks in the trace
# as the measurement window; it does not need the rank<->device mapping.
import sys, re, json, argparse
from collections import defaultdict

ts_re   = re.compile(r'^\[(\d\d):(\d\d):(\d\d)\.(\d{9})\]')
ev_re   = re.compile(r'(pmpi|roctracer|pinsight_enter_exit|energy)_pinsight_lttng_ust:(\w+)')
rank_re = re.compile(r'mpirank = (\d+)')
act_re  = re.compile(r'devId = (\d+), correlation_id = \d+, begin_ns = (\d+), end_ns = (\d+)')
bytes_re= re.compile(r'bytes = (\d+)')
kname_re= re.compile(r'kernel_name = "([^"]*)"')
# gpu_uj prints as: gpu_uj = [ [0] = N, [1] = N, ... ], seq = 0  (nested brackets)
gpuj_re = re.compile(r'gpu_uj = \[(.*?)\], seq')
gpuj_val= re.compile(r'\[\d+\] = (\d+)')

MPI_WAIT = {"MPI_Waitall","MPI_Wait","MPI_Allreduce","MPI_Barrier","MPI_Allgather"}

def _cxxfilt_path():
    import os, shutil
    for p in (os.path.join(os.environ.get("ROCM_PATH","/opt/rocm"),"llvm/bin/llvm-cxxfilt"),
              "llvm-cxxfilt","c++filt"):
        if os.path.isabs(p):
            if os.path.exists(p): return p
        elif shutil.which(p): return p
    return None

def demangle_simplify(names):
    """mangled -> short readable kernel label: drop return type/args/template args,
    collapse rocPRIM/thrust version namespaces, keep namespace::function."""
    import subprocess
    raw = {}
    cf = _cxxfilt_path()
    if cf:
        try:
            out = subprocess.run([cf], input="\n".join(names), capture_output=True,
                                  text=True).stdout.splitlines()
            if len(out)==len(names): raw = dict(zip(names,out))
        except Exception: raw = {}
    def simp(name):
        d = raw.get(name, name)
        # strip balanced <...> (template args) and (...) (call args), iteratively
        for open_c,close_c in (("<",">"),("(",")")):
            while open_c in d:
                i=d.index(open_c); depth=0; j=i
                for j in range(i,len(d)):
                    depth += d[j]==open_c; depth -= d[j]==close_c
                    if depth==0: break
                d = d[:i]+d[j+1:]
        d = d.replace("void ","").strip()
        d = re.sub(r'(ROCPRIM|THRUST)_[0-9_]+_NS::','',d)   # version namespaces
        d = re.sub(r'\bdetail::','',d)
        d = d.split()[-1] if d.split() else d                # drop any leading ret-type token
        parts = d.split("::")
        return "::".join(parts[-2:]) if len(parts)>1 else (parts[-1] if parts else d)
    return {n:simp(n) for n in names}
# host time categories summed from begin->end durations
def cat_of(prov, base):
    if prov == "pmpi":
        return ("mpi_all","mpi_wait") if base in MPI_WAIT else ("mpi_all",)
    if base in ("hipStreamSync","hipDeviceSync"): return ("gpu_sync",)
    if base == "hipKernelLaunch": return ("launch",)
    if base in ("hipMemcpy","hipMemcpyAsync"): return ("memcpy",)
    return ()

def to_ns(m):
    h,mi,s,f = (int(x) for x in m)
    return ((h*3600+mi*60+s)*1_000_000_000)+f

def parse(stream):
    dur   = defaultdict(lambda: defaultdict(float))   # dur[rank][cat] seconds (host)
    opent = defaultdict(dict)
    first = {}; last = {}
    launch_count = defaultdict(int)
    mpi_count    = defaultdict(lambda: defaultdict(int))
    # activity: keyed by physical devId
    gpu_exec_ns  = defaultdict(int); gpu_kern_n = defaultdict(int)
    gpu_copy_ns  = defaultdict(int); gpu_bytes  = defaultdict(int)
    kern_exec_ns = defaultdict(int); kern_n = defaultdict(int)   # by kernel name (node)
    dev_seen = set()
    # energy: first enter / last exit gpu_uj vector
    e_enter = None; e_enter_t = None; e_exit = None; e_exit_t = None

    for line in stream:
        tm = ts_re.match(line)
        if not tm: continue
        t = to_ns(tm.groups())
        em = ev_re.search(line)
        if not em: continue
        prov, ev = em.group(1), em.group(2)

        if ev == "energy_enter":
            g = gpuj_re.search(line)
            if g:
                vec = [int(v) for v in gpuj_val.findall(g.group(1))]
                if e_enter is None or t < e_enter_t: e_enter, e_enter_t = vec, t
            continue
        if ev == "energy_exit":
            g = gpuj_re.search(line)
            if g:
                vec = [int(v) for v in gpuj_val.findall(g.group(1))]
                if e_exit is None or t > e_exit_t: e_exit, e_exit_t = vec, t
            continue

        if ev == "hipKernelActivity":
            a = act_re.search(line)
            if a:
                d = int(a.group(1)); ex = int(a.group(3))-int(a.group(2))
                gpu_exec_ns[d]+=ex; gpu_kern_n[d]+=1; dev_seen.add(d)
                kn = kname_re.search(line)
                if kn: kern_exec_ns[kn.group(1)]+=ex; kern_n[kn.group(1)]+=1
            continue
        if ev == "hipMemcpyActivity":
            a = act_re.search(line)
            if a:
                d = int(a.group(1)); gpu_copy_ns[d]+=int(a.group(3))-int(a.group(2))
                b = bytes_re.search(line)
                if b: gpu_bytes[d]+=int(b.group(1))
            continue

        rm = rank_re.search(line)
        if not rm: continue
        r = int(rm.group(1))

        if r not in first: first[r] = t
        last[r] = t
        if ev.endswith("_begin"):
            opent[r][ev[:-6]] = t
        elif ev.endswith("_end"):
            base = ev[:-4]
            b = opent[r].pop(base, None)
            if b is None: continue
            d = (t-b)/1e9
            if base == "hipKernelLaunch": launch_count[r]+=1
            if prov == "pmpi": mpi_count[r][base]+=1
            for c in cat_of(prov, base): dur[r][c]+=d

    ranks = sorted(set(first)|set(dur))
    # devId -> rank: ONLY when unambiguous (exactly one physical device per
    # rank -- the common one-GPU-per-rank deployment). If counts don't match
    # (shared GPUs, CPU-only run, etc.) leave the map empty rather than guess.
    dev_sorted = sorted(dev_seen)
    dev2rank = {d: r for d, r in zip(dev_sorted, ranks)} if len(dev_sorted) == len(ranks) else {}

    return dict(ranks=ranks, dur=dur, first=first, last=last, launch_count=launch_count,
                mpi_count=mpi_count, gpu_exec_ns=gpu_exec_ns, gpu_kern_n=gpu_kern_n,
                gpu_copy_ns=gpu_copy_ns, gpu_bytes=gpu_bytes, kern_exec_ns=kern_exec_ns,
                kern_n=kern_n, dev2rank=dev2rank,
                e_enter=e_enter, e_exit=e_exit, e_enter_t=e_enter_t, e_exit_t=e_exit_t)

def per_rank_rows(R):
    rank2dev = {r: d for d, r in R['dev2rank'].items()}
    rows=[]
    for r in R['ranks']:
        wall = (R['last'].get(r,0)-R['first'].get(r,0))/1e9 if r in R['first'] else 0
        d = R['dur'][r]
        dev = rank2dev.get(r)             # None if rank<->device mapping is ambiguous
        gexec = R['gpu_exec_ns'].get(dev,0)/1e9 if dev is not None else 0.0
        gbytes= R['gpu_bytes'].get(dev,0) if dev is not None else 0
        rows.append(dict(rank=r, dev=dev, wall=wall, mpi=d['mpi_all'], mpi_wait=d['mpi_wait'],
                         gpu_sync=d['gpu_sync'], launch=d['launch'], memcpy=d['memcpy'],
                         kernels=R['launch_count'][r], gpu_exec=gexec, gbytes=gbytes))
    return rows

def top_kernels_agg(R, n):
    """Aggregate kernel exec time by simplified (demangled) name -> top n
    [(nice_name, exec_s, count)]. Many mangled template instantiations of e.g.
    rocprim::trampoline_kernel collapse to one launcher function."""
    nice = demangle_simplify(list(R['kern_exec_ns'].keys()))
    agg_ns = defaultdict(int); agg_n = defaultdict(int)
    for name, ns in R['kern_exec_ns'].items():
        agg_ns[nice[name]] += ns; agg_n[nice[name]] += R['kern_n'][name]
    tops = sorted(agg_ns.items(), key=lambda kv: -kv[1])[:n]
    return [(nm, ns/1e9, agg_n[nm]) for nm, ns in tops]

def print_tables(R, rows):
    if not R['dev2rank'] and R['ranks']:
        print("[note: rank<->GPU-device mapping is ambiguous (device count != rank "
              "count) -- per-rank GPU-exec time below is reported as 0; node-wide "
              "GPU stats further down are unaffected]")
    print(f"\n{'rank':>4} {'wall(s)':>8} {'MPI(s)':>8} {'MPIwait':>8} {'GPUsync':>8} "
          f"{'launch':>7} {'memcpy':>7} {'GPUexec':>8} {'kernels':>8}   MPI%  GPUexec%")
    print("-"*98)
    for x in rows:
        w=x['wall'] or 1
        print(f"{x['rank']:>4} {x['wall']:8.2f} {x['mpi']:8.3f} {x['mpi_wait']:8.3f} "
              f"{x['gpu_sync']:8.3f} {x['launch']:7.3f} {x['memcpy']:7.3f} {x['gpu_exec']:8.3f} "
              f"{x['kernels']:8d}  {100*x['mpi']/w:5.1f}  {100*x['gpu_exec']/w:6.1f}")
    print("-"*98)
    def imb(v): return (max(v)-min(v))/(sum(v)/len(v))*100 if v and sum(v) else 0
    mpis=[x['mpi'] for x in rows]; gex=[x['gpu_exec'] for x in rows]
    print(f"load imbalance (max-min/mean):  MPI {imb(mpis):.0f}%   GPU-exec {imb(gex):.0f}%")

    # energy per device (node-wide counter; not rank-attributed)
    if R['e_enter'] and R['e_exit']:
        secs = (R['e_exit_t']-R['e_enter_t'])/1e9
        print(f"\nPer-device energy (first-enter -> last-exit, window {secs:.2f}s; "
              f"node-wide counters, same for every rank on that node):")
        print(f"{'dev':>4} {'Joules':>10} {'avg W':>8}")
        for i,(a,b) in enumerate(zip(R['e_enter'], R['e_exit'])):
            J=(b-a)/1e6
            print(f"{i:>4} {J:10.1f} {J/secs if secs else 0:8.1f}")

    # GPU kernel hotspots (node-total), aggregated by simplified name
    tot  = sum(R['kern_exec_ns'].values())/1e9 or 1
    print(f"\nTop GPU kernels by node-total exec time ({tot:.3f}s over "
          f"{sum(R['kern_n'].values())} launches):")
    print(f"{'exec(s)':>9} {'%':>5} {'count':>7}  kernel")
    for nm,sec,cnt in top_kernels_agg(R, 12):
        lbl = nm if len(nm)<=60 else nm[:57]+"..."
        print(f"{sec:9.3f} {100*sec/tot:5.1f} {cnt:7d}  {lbl}")

def make_figures(R, rows, outdir, tag):
    import os
    import matplotlib; matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    import numpy as np
    os.makedirs(outdir, exist_ok=True)
    ranks=[x['rank'] for x in rows]
    y=np.arange(len(ranks))

    # ---- Fig 1: per-rank host-time breakdown (stacked) -> load imbalance ----
    cats=[("MPI wait","mpi_wait","#d62728"),("GPU sync","gpu_sync","#1f77b4"),
          ("kernel launch","launch","#ff7f0e"),("memcpy","memcpy","#2ca02c")]
    fig,ax=plt.subplots(figsize=(8,0.7*len(ranks)+1.6))
    left=np.zeros(len(ranks))
    for label,key,c in cats:
        v=np.array([x[key] for x in rows])
        ax.barh(y,v,left=left,color=c,label=label); left+=v
    other=np.array([max(x['wall']-left[i],0) for i,x in enumerate(rows)])
    ax.barh(y,other,left=left,color="#cccccc",label="other (compute/idle)")
    ax.set_yticks(y)
    ax.set_yticklabels([f"rank {r}" + (f"\n(dev {x['dev']})" if x['dev'] is not None else "")
                         for r,x in zip(ranks,rows)])
    ax.set_xlabel("wall-clock time (s)"); ax.invert_yaxis()
    ax.set_title(f"Per-rank time breakdown ({tag})")
    ax.legend(ncol=3,fontsize=8,loc="lower right")
    fig.tight_layout(); f1=os.path.join(outdir,f"{tag}_time_breakdown.png")
    fig.savefig(f1,dpi=150); plt.close(fig)

    # ---- Fig 2: per-device energy (J) + avg power ----
    if R['e_enter'] and R['e_exit']:
        secs=(R['e_exit_t']-R['e_enter_t'])/1e9
        J=[(b-a)/1e6 for a,b in zip(R['e_enter'],R['e_exit'])]
        W=[j/secs if secs else 0 for j in J]
        fig,ax=plt.subplots(figsize=(7,4))
        x=np.arange(len(J)); bars=ax.bar(x,J,color="#9467bd")
        ax.set_xticks(x); ax.set_xticklabels([f"device {i}" for i in x])
        ax.set_ylabel("energy (J)")
        ax.set_ylim(0, max(J)*1.15)
        ax.set_title(f"Per-device energy ({tag}, {secs:.1f}s)")
        for xi,(j,w) in enumerate(zip(J,W)):
            ax.text(xi,j,f"{w:.0f} W",ha="center",va="bottom",fontsize=9)
        fig.tight_layout()
        f2=os.path.join(outdir,f"{tag}_energy_per_device.png"); fig.savefig(f2,dpi=150); plt.close(fig)

    # ---- Fig 3: top GPU kernels by node-total exec time ----
    tops=top_kernels_agg(R, 10)
    if tops:
        def short(s): return (s[:48]+"...") if len(s)>51 else s
        names=[short(nm) for nm,_,_ in tops][::-1]; vals=[sec for _,sec,_ in tops][::-1]
        fig,ax=plt.subplots(figsize=(9,0.5*len(names)+1.5))
        ax.barh(np.arange(len(names)),vals,color="#17becf")
        ax.set_yticks(np.arange(len(names))); ax.set_yticklabels(names,fontsize=7)
        ax.set_xlabel("node-total GPU exec time (s)")
        ax.set_title(f"Top GPU kernels ({tag})")
        fig.tight_layout(); f3=os.path.join(outdir,f"{tag}_top_kernels.png")
        fig.savefig(f3,dpi=150); plt.close(fig)
    print(f"\nFigures written to {outdir}/ (prefix {tag}_)")

def main():
    ap=argparse.ArgumentParser(
        description="Per-rank load-balance, GPU-utilization, and energy report "
                    "from a PInsight trace of an MPI+HIP(+energy) application.")
    ap.add_argument("--outdir",default="figures")
    ap.add_argument("--tag",default="run")
    ap.add_argument("--json",help="write machine-readable summary here")
    ap.add_argument("--no-figures",action="store_true")
    a=ap.parse_args()
    R=parse(sys.stdin)
    rows=per_rank_rows(R)
    print_tables(R,rows)
    if a.json:
        summary=dict(rows=rows,
                     energy=dict(enter=R['e_enter'],exit=R['e_exit'],
                                 window_s=(R['e_exit_t']-R['e_enter_t'])/1e9
                                          if R['e_enter'] and R['e_exit'] else None),
                     top_kernels=[dict(kernel=nm,exec_s=sec,count=cnt)
                                  for nm,sec,cnt in top_kernels_agg(R,20)])
        json.dump(summary,open(a.json,"w"),indent=2)
        print(f"summary JSON -> {a.json}")
    if not a.no_figures:
        try: make_figures(R,rows,a.outdir,a.tag)
        except Exception as e: print(f"[figures skipped: {e}]")

if __name__=="__main__":
    main()
