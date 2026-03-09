#!/usr/bin/env python3
"""
Parse raw output from run_bench.sh and generate markdown tables.

Usage:
    bash run_bench.sh 512 512 | tee results.txt
    python3 parse_bench.py results.txt

    # Or pipe directly:
    bash run_bench.sh 512 512 | python3 parse_bench.py

Output: Two markdown tables (median execution time + overhead %).
"""

import sys
import statistics

# Map raw labels to display names
LABEL_MAP = {
    "BASELINE":         "**Baseline**",
    "OFF":              "**OFF**",
    "MONITORING":       "**MONITORING**",
    "TRACING_nosess":   "**TRACING** (no session)",
    "TRACING_sess_full":"**TRACING** (with session)",
    "TRACING_sess_r100":"**TRACING** (rate=50, max=100)",
}

CONFIG_ORDER = [
    "BASELINE", "OFF", "MONITORING",
    "TRACING_nosess", "TRACING_sess_full", "TRACING_sess_r100"
]

def parse(lines):
    """Parse run_bench.sh output -> {thread_count: {config_label: [times]}}"""
    data = {}       # thread -> {label -> [times]}
    current_t = None

    for line in lines:
        line = line.strip()
        if line.startswith("--- OMP_NUM_THREADS="):
            current_t = int(line.split("=")[1].split()[0])
            data[current_t] = {}
        elif ":" in line and current_t is not None:
            parts = line.split(":")
            label = parts[0].strip()
            if label in LABEL_MAP:
                vals = parts[1].strip().split()
                times = [int(v) for v in vals if v.isdigit()]
                if times:
                    data[current_t][label] = times
    return data


def median(vals):
    return int(statistics.median(vals))


def make_tables(data):
    threads = sorted(data.keys())
    thread_headers = [f"{t}T" for t in threads]

    # Compute medians
    medians = {}  # config -> [median_per_thread]
    for cfg in CONFIG_ORDER:
        row = []
        for t in threads:
            vals = data.get(t, {}).get(cfg, [])
            row.append(median(vals) if vals else None)
        medians[cfg] = row

    # --- Table 1: Median Execution Time ---
    lines = []
    lines.append("#### Median Execution Time (ms)\n")
    lines.append("| Config | " + " | ".join(thread_headers) + " |")
    lines.append("|---|" + "|".join(["---"] * len(threads)) + "|")
    for cfg in CONFIG_ORDER:
        display = LABEL_MAP.get(cfg, cfg)
        vals = [str(m) if m is not None else "—" for m in medians[cfg]]
        lines.append(f"| {display} | " + " | ".join(vals) + " |")
    lines.append("")

    # --- Table 2: Overhead % ---
    lines.append("#### Overhead % vs Baseline\n")
    lines.append("| Config | " + " | ".join(thread_headers) + " |")
    lines.append("|---|" + "|".join(["---"] * len(threads)) + "|")
    baseline = medians.get("BASELINE", [])
    for cfg in CONFIG_ORDER:
        if cfg == "BASELINE":
            continue
        display = LABEL_MAP.get(cfg, cfg)
        pcts = []
        for i, t in enumerate(threads):
            m = medians[cfg][i]
            b = baseline[i] if i < len(baseline) else None
            if m is not None and b is not None and b > 0:
                pct = (m - b) / b * 100
                pcts.append(f"{pct:+.1f}%")
            else:
                pcts.append("—")
        lines.append(f"| {display} | " + " | ".join(pcts) + " |")
    lines.append("")

    return "\n".join(lines)


def main():
    if len(sys.argv) > 1:
        with open(sys.argv[1]) as f:
            lines = f.readlines()
    else:
        lines = sys.stdin.readlines()

    data = parse(lines)
    if not data:
        print("ERROR: No benchmark data found. Expected run_bench.sh output.",
              file=sys.stderr)
        sys.exit(1)

    print(make_tables(data))


if __name__ == "__main__":
    main()
