#!/usr/bin/env python3
# GPU kernel concurrency + kernel-time distribution from a PInsight trace.
#
#   usage: python3 gpu_kernel_concurrency.py <trace_dir> [<trace_dir> ...]
#
# Written for the CUPTI serialization question (pinsight commit 2384cd4):
# CUPTI_ACTIVITY_KIND_KERNEL and CUPTI_ACTIVITY_KIND_CONCURRENT_KERNEL are
# mutually exclusive, and collecting on the KERNEL kind SERIALIZES kernel
# execution on the GPU.  That is invisible in event counts -- the trace looks
# identical -- and only shows up in how kernel intervals overlap in time.
#
# The detector is the overlap factor, per physical device:
#
#     overlap = sum(kernel durations) / |union of kernel intervals|
#
# A device that runs kernels strictly one-at-a-time has overlap == 1.00 by
# construction, no matter how fast or slow it is.  Anything above 1.0 means
# kernels were resident together.  Reporting the busy-span union (rather than
# wall time) keeps the metric independent of how much idle/host time the run
# contains, so it stays comparable across configurations.
#
# max_concurrent is the peak number of simultaneously-resident kernels, from a
# sweep line over interval endpoints; it corroborates the overlap factor and
# is the more legible number when overlap is only slightly above 1.
#
# Works for both vendors: reads cudaKernelActivity and hipKernelActivity.
import sys
from collections import defaultdict
from pinsight_reader import events, percentile

ACT_NAMES = ("cudaKernelActivity", "hipKernelActivity")


def collect(dirs):
    # per physical device: list of (start_ns, end_ns), plus stream/name tallies
    per_dev = defaultdict(list)
    streams = defaultdict(set)
    names = defaultdict(lambda: defaultdict(int))
    vpids = defaultdict(set)
    for ev in events(dirs):
        if ev.name not in ACT_NAMES:
            continue
        s, e = ev.i("start_gpu"), ev.i("end_gpu")
        if s is None or e is None or e < s:
            continue
        dev = ev.i("devId", -1)
        per_dev[dev].append((s, e))
        sid = ev.i("streamId")
        if sid is not None:
            streams[dev].add(sid)
        kn = ev.s("kernelName")
        if kn:
            names[dev][kn] += 1
        vp = ev.i("vpid")
        if vp is not None:
            vpids[dev].add(vp)
    return per_dev, streams, names, vpids


def union_ns(intervals):
    """Total time covered by the union of [start, end) intervals."""
    if not intervals:
        return 0
    iv = sorted(intervals)
    total = 0
    cs, ce = iv[0]
    for s, e in iv[1:]:
        if s > ce:               # disjoint: close the open run
            total += ce - cs
            cs, ce = s, e
        elif e > ce:             # overlapping: extend
            ce = e
    return total + (ce - cs)


def max_concurrent(intervals):
    """Peak simultaneously-resident kernels (sweep line over endpoints).
    Ends are processed before starts at equal timestamps so that a kernel
    ending exactly when the next begins is not miscounted as overlap."""
    pts = []
    for s, e in intervals:
        pts.append((s, 1))
        pts.append((e, 0))
    pts.sort(key=lambda p: (p[0], p[1]))
    cur = peak = 0
    for _, is_start in pts:
        cur += 1 if is_start else -1
        peak = max(peak, cur)
    return peak


def report(dirs):
    per_dev, streams, names, vpids = collect(dirs)
    if not per_dev:
        print("no kernel activity records found in:", " ".join(dirs))
        return 1

    print(f"{'dev':>4} {'kernels':>9} {'streams':>8} {'busy_s':>10} "
          f"{'sum_s':>10} {'overlap':>8} {'maxcc':>6} "
          f"{'mean_us':>9} {'p50_us':>9} {'p95_us':>9} {'max_us':>10}")
    print("-" * 106)
    tot_n = tot_sum = tot_union = 0
    worst_overlap = None
    for dev in sorted(per_dev):
        iv = per_dev[dev]
        durs = sorted(e - s for s, e in iv)
        ssum = sum(durs)
        uni = union_ns(iv)
        ov = (ssum / uni) if uni else 0.0
        mc = max_concurrent(iv)
        tot_n += len(iv); tot_sum += ssum; tot_union += uni
        worst_overlap = ov if worst_overlap is None else min(worst_overlap, ov)
        print(f"{dev:>4} {len(iv):>9} {len(streams[dev]):>8} {uni/1e9:>10.3f} "
              f"{ssum/1e9:>10.3f} {ov:>8.2f} {mc:>6} "
              f"{ssum/len(durs)/1e3:>9.1f} {percentile(durs,50)/1e3:>9.1f} "
              f"{percentile(durs,95)/1e3:>9.1f} {durs[-1]/1e3:>10.1f}")

    print("-" * 106)
    print(f"total kernels: {tot_n}   sum kernel time: {tot_sum/1e9:.3f} s   "
          f"union busy: {tot_union/1e9:.3f} s")
    for dev in sorted(per_dev):
        if vpids[dev]:
            print(f"  dev {dev}: collected by vpid(s) "
                  f"{sorted(vpids[dev])}, {len(names[dev])} distinct kernels")

    # Interpretation. overlap == 1.00 on every device with >1 stream in flight
    # is the serialization signature; it cannot occur by chance on a real
    # multi-stream workload.
    print()
    multi = [d for d in per_dev if len(streams[d]) > 1]
    if multi and worst_overlap is not None and worst_overlap < 1.001:
        print("VERDICT: SERIALIZED — overlap 1.00 on every device despite "
              f"{max(len(streams[d]) for d in multi)} streams in flight.")
    elif worst_overlap is not None and worst_overlap >= 1.001:
        print(f"VERDICT: CONCURRENT — kernels overlap (min overlap factor "
              f"{worst_overlap:.2f} across devices).")
    else:
        print("VERDICT: inconclusive — only one stream per device in this "
              "run, so there is nothing to overlap. Not evidence either way.")
    return 0


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print(__doc__.strip())
        sys.exit(2)
    sys.exit(report(sys.argv[1:]))
