#!/usr/bin/env python3
# GENERIC TraceCompass LAMI adapter: converts any analysis script's neutral
# table contract (TITLE / DESCRIPTION / TABLE_SPECS / build_tables — see
# pinsight_reader.py) into the LAMI 1.0 protocol. One adapter serves every
# analysis, current and future — no per-analysis LAMI code needed.
#
#   usage: lami_adapter.py <analysis_module> [lami args...] <trace...>
#   e.g.:  lami_adapter.py load_imbalance --lami /path/to/trace
#
# (tc-wrapper.sh supplies <analysis_module> from its ANALYSIS config line.)
import importlib, sys
import lami                     # bootstraps sys.path to the analyses folder

# neutral column type -> LAMI column class
_LAMI_CLASS = {"int": "int", "string": "string", "number": "number",
               "duration_s": "duration", "ratio": "ratio", "bytes": "size"}

def _cell(value, ctype):
    if ctype == "duration_s": return lami.duration(value)
    if ctype == "ratio":      return lami.ratio(value)
    if ctype == "bytes":      return lami.size_bytes(value)
    return value               # int / string / number: plain JSON value

def main():
    if len(sys.argv) < 2:
        print("usage: lami_adapter.py <analysis_module> [args...]",
              file=sys.stderr)
        return 1
    mod = importlib.import_module(sys.argv[1])
    meta = lami.metadata(
        mod.TITLE, mod.DESCRIPTION,
        {name: (spec["title"],
                [(cname, _LAMI_CLASS[ctype])
                 for cname, ctype in spec["columns"]])
         for name, spec in mod.TABLE_SPECS.items()})

    def analyze_lami(dirs, b_tod, e_tod):
        tables_rows, span = mod.build_tables(dirs, b_tod, e_tod)
        out = []
        for name, spec in mod.TABLE_SPECS.items():
            types = [t for _, t in spec["columns"]]
            rows = [[_cell(v, t) for v, t in zip(row, types)]
                    for row in tables_rows.get(name, [])]
            out.append((name, rows))
        return out, span

    return lami.run(sys.argv[2:], meta, analyze_lami,
                    getattr(mod, "main", lambda dirs: None),
                    f"usage: lami_adapter.py {sys.argv[1]} <trace_or_folder>...")

if __name__ == "__main__":
    sys.exit(main())
