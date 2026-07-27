#!/usr/bin/env python3
import re, subprocess, sys

def parse_trace(path):
    out = subprocess.run(["babeltrace2", path], capture_output=True, text=True, env=None)
    lines = out.stdout.splitlines()
    enters, exits = [], []
    ts_re = re.compile(r'^\[(\d+):(\d+):(\d+)\.(\d+)\]')
    gpu_re = re.compile(r'gpu_uj = \[ (.*?) \]')
    for line in lines:
        if "energy_pinsight_lttng_ust:energy_enter" not in line and \
           "energy_pinsight_lttng_ust:energy_exit" not in line:
            continue
        m = ts_re.match(line)
        h, mi, s, ns = m.groups()
        t = int(h)*3600 + int(mi)*60 + int(s) + int(ns)/1e9
        g = gpu_re.search(line)
        vals = [int(x.split('=')[1].strip()) for x in g.group(1).split(',')] if g.group(1).strip() else []
        entry = (t, vals)
        if "energy_enter" in line:
            enters.append(entry)
        else:
            exits.append(entry)
    if not enters or not exits:
        return None
    enter = min(enters, key=lambda x: x[0])
    exit_ = max(exits, key=lambda x: x[0])
    dt = exit_[0] - enter[0]
    joules = [(e - s)/1e6 for s, e in zip(enter[1], exit_[1])]
    watts = [j/dt for j in joules]
    return dt, joules, watts

if __name__ == "__main__":
    for p in sys.argv[1:]:
        r = parse_trace(p)
        if r is None:
            print(f"{p}: no energy_enter/exit events found")
            continue
        dt, joules, watts = r
        print(f"{p}: elapsed={dt:.2f}s")
        for i, (j, w) in enumerate(zip(joules, watts)):
            print(f"  GPU{i}: {j:9.1f} J  {w:7.1f} W")
        print(f"  TOTAL: {sum(joules):9.1f} J  {sum(watts):7.1f} W (sum of 4 APUs)")
