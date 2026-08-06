#!/usr/bin/env python3
# Per-node GPU energy from PInsight traces (energy domain only).
#
# Armed-span model (2026-07-31; pinsight-eval/docs/design/energy_power_implementation_plan.md (private dev repo)):
# energy measurement runs in ARMED SPANS — the energy_enter/energy_exit pair of
# span i shares seq = i (per process), and a run that never reconfigures has
# exactly one span. This script pairs enter/exit by (hostname, pid, seq), SUMS
# per-device deltas over complete spans, and skips incomplete spans (e.g. a
# windowed capture slicing a span). gpu_uj is NODE-WIDE — every measuring rank
# on a node reports the same values — so one representative pid per host is
# used (the one with the most measured time). Watts = Joules / measured time
# (sum of span durations), i.e. average power while armed.
import re, subprocess, sys
from collections import defaultdict

ts_re  = re.compile(r'^\[(\d+):(\d+):(\d+)\.(\d+)\]')
# gpu_uj prints as: gpu_uj = [ [0] = N, [1] = N, ... ], seq = S (nested brackets)
gpu_re = re.compile(r'gpu_uj = \[(.*?)\], seq')
val_re = re.compile(r'\[\d+\] = (\d+)')
hp_re  = re.compile(r'hostname = "([^"]*)", pid = (\d+)')
seq_re = re.compile(r'seq = (\d+)')

def parse_trace(path):
    out = subprocess.run(["babeltrace2", path], capture_output=True, text=True, env=None)
    # (host, pid) -> seq -> {'enter': (t, vals), 'exit': (t, vals)}
    spans = defaultdict(lambda: defaultdict(dict))
    for line in out.stdout.splitlines():
        if "energy_pinsight_lttng_ust:energy_enter" in line:
            kind = 'enter'
        elif "energy_pinsight_lttng_ust:energy_exit" in line:
            kind = 'exit'
        else:
            continue
        m = ts_re.match(line); hp = hp_re.search(line); sq = seq_re.search(line)
        if not (m and hp and sq):
            continue
        h, mi, s, ns = m.groups()
        t = int(h)*3600 + int(mi)*60 + int(s) + int(ns)/1e9
        g = gpu_re.search(line)
        vals = [int(v) for v in val_re.findall(g.group(1))] if g else []
        spans[(hp.group(1), int(hp.group(2)))][int(sq.group(1))][kind] = (t, vals)

    # Sum complete spans per (host, pid), then keep one representative pid per
    # host (node-wide counters: measuring ranks on a node report duplicates).
    best = {}
    for (host, pid), by_seq in spans.items():
        joules = None; secs = 0.0; complete = 0; incomplete = 0
        for s in sorted(by_seq):
            pair = by_seq[s]
            if 'enter' not in pair or 'exit' not in pair:
                incomplete += 1
                continue
            (t0, v0), (t1, v1) = pair['enter'], pair['exit']
            complete += 1; secs += t1 - t0
            d = [(b - a)/1e6 for a, b in zip(v0, v1)]
            joules = d if joules is None else [x + y for x, y in zip(joules, d)]
        e = dict(pid=pid, joules=joules or [], measured_s=secs,
                 spans=complete, incomplete=incomplete)
        if host not in best or e['measured_s'] > best[host]['measured_s']:
            best[host] = e
    return best

if __name__ == "__main__":
    for p in sys.argv[1:]:
        hosts = parse_trace(p)
        if not hosts:
            print(f"{p}: no energy_enter/exit events found")
            continue
        for host, e in sorted(hosts.items()):
            note = f", {e['incomplete']} incomplete span(s) skipped" if e['incomplete'] else ""
            print(f"{p}: {host} (pid {e['pid']}): {e['spans']} span(s), "
                  f"measured={e['measured_s']:.2f}s{note}")
            if not e['spans']:
                print("  [no complete enter/exit span pair]")
                continue
            secs = e['measured_s']
            for i, j in enumerate(e['joules']):
                print(f"  GPU{i}: {j:9.1f} J  {j/secs if secs else 0:7.1f} W")
            print(f"  TOTAL: {sum(e['joules']):9.1f} J  "
                  f"{sum(e['joules'])/secs if secs else 0:7.1f} W "
                  f"(sum of {len(e['joules'])} devices)")
