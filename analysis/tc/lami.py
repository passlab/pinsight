#!/usr/bin/env python3
# Shared LAMI 1.0 support for the PInsight analysis scripts — the TraceCompass
# "External Analyses" protocol, validated against TC 2026-06 on 2026-07-20
# (see doc/visualization_analysis_redesign.md WS9).
#
# Protocol (LamiAnalysis.canExecute() / execute() in TC source):
#   1. `<cmd> --mi-version`                    -> print "1.0" (REQUIRED; TC
#      marks the analysis unsupported/struck-through without it)
#   2. `<cmd> --metadata`                      -> table-schema JSON
#   3. `<cmd> --test-compatibility <trace>`    -> exit 0 if compatible
#   4. `<cmd> [--output-progress] [--begin ns --end ns] [extra args] <trace>`
#      -> results JSON. Every result object MUST carry "time-range" (TC
#      errors with 'JSONObject["time-range"] not found' otherwise).
#
# TC quirks handled here:
#   - TC treats the configured command as ONE executable (no shell splitting):
#     use a single-word wrapper script that pins interpreter/paths/PATH
#     (GUI apps get a minimal PATH: babeltrace2 & python must be resolvable).
#   - --begin/--end arrive as absolute epoch ns; babeltrace2's default text
#     output (what pinsight_reader parses) is LOCAL time-of-day, so bounds
#     are converted (ranges spanning local midnight unsupported).
#   - Extra arguments typed in TC's run dialog arrive before the trace path;
#     we treat every non-flag argument as another trace dir, so pasting the
#     other nodes' trace dirs there yields a merged multi-node analysis.
import json, os, sys, time

# The analysis scripts live one level up; make them importable from here.
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from pinsight_reader import expand_dirs

def duration(seconds):
    return {"class": "duration", "value": int(seconds * 1e9)}

def duration_ns(ns):
    return {"class": "duration", "value": int(ns)}

def ratio(v):
    return {"class": "ratio", "value": round(v, 4)}

def size_bytes(n):
    return {"class": "size", "value": int(n)}

def metadata(title, description, tables):
    """tables: {class_id: (table_title, [(col_title, col_class), ...])}"""
    return {
        "mi-version": {"major": 1, "minor": 0},
        "version": {"major": 0, "minor": 2, "patch": 0},
        "title": title,
        "description": description,
        "table-classes": {
            cid: {"title": ttitle,
                  "column-descriptions": [{"title": t, "class": c}
                                          for t, c in cols]}
            for cid, (ttitle, cols) in tables.items()
        },
    }

def epoch_ns_to_local_tod_ns(epoch_ns):
    lt = time.localtime(epoch_ns // 1_000_000_000)
    frac = epoch_ns % 1_000_000_000
    return ((lt.tm_hour*3600 + lt.tm_min*60 + lt.tm_sec) * 1_000_000_000) + frac

def results(tables_rows, span, begin_epoch_ns=None, end_epoch_ns=None):
    """tables_rows: [(class_id, [row, ...]), ...]; span: (t0_ns, t1_ns)."""
    if begin_epoch_ns is not None and end_epoch_ns is not None:
        tr = {"class": "time-range", "begin": begin_epoch_ns, "end": end_epoch_ns}
    else:
        tr = {"class": "time-range", "begin": span[0], "end": span[1]}
    return {"results": [{"time-range": tr, "class": cid, "data": rows}
                        for cid, rows in tables_rows]}

def is_pinsight_trace(trace_dir):
    """True if the CTF trace dir contains PInsight events — determined by
    scanning the trace's `metadata` (TSDL, which names all event providers)
    for the `_pinsight_lttng_ust` provider suffix. Works on plain-text and
    packetized metadata alike (binary-safe substring search)."""
    import os
    try:
        with open(os.path.join(trace_dir, "metadata"), "rb") as f:
            return b"pinsight_lttng_ust" in f.read()
    except OSError:
        return False

def run(argv, meta, analyze_lami, main_text, usage):
    """Standard CLI for a LAMI-capable analysis script.
    meta: the metadata() dict.
    analyze_lami(dirs, begin_tod_ns, end_tod_ns) -> (tables_rows, span)
    main_text(dirs) -> None  (original human-readable mode)
    """
    lami = False; begin = None; end = None; test_compat = False; raw = []
    i = 0
    while i < len(argv):
        a = argv[i]
        if a == "--lami": lami = True
        elif a == "--mi-version":
            print("1.0"); return 0
        elif a == "--metadata":
            print(json.dumps(meta)); return 0
        elif a == "--test-compatibility": test_compat = True
        elif a == "--begin": i += 1; begin = int(argv[i])
        elif a == "--end":   i += 1; end = int(argv[i])
        elif a == "--output-progress": pass   # progress not implemented
        elif a.startswith("--"): pass         # tolerate unknown TC flags
        else: raw.append(a)
        i += 1

    if test_compat:
        # TC calls `--test-compatibility <trace.getPath()>` per trace at
        # selection/display time (LamiAnalysis.testCompatibility) to decide
        # whether to grey the analysis out for that trace.
        #
        # CRITICAL, verified from the TC 10.2 jar (LamiAnalysis +
        # common.core ProcessUtils): TC *ignores this command's exit code*.
        # It captures stdout with STDERR MERGED IN (ProcessBuilder
        # redirectErrorStream(true)) and rules:
        #     empty output                       -> compatible
        #     valid JSON without "error-message" -> compatible
        #     anything else (a stray stderr line,
        #        non-JSON text, or JSON w/ error) -> INCOMPATIBLE (struck out)
        # So we must be completely SILENT (no stdout AND no stderr) when
        # compatible, and print a JSON {"error-message": ...} when not.
        # expand_dirs() writes warnings to stderr for non-dir arguments;
        # under the merged stream those read as non-JSON and would falsely
        # mark a trace incompatible, so we silence it here.
        import io, contextlib
        with contextlib.redirect_stderr(io.StringIO()):
            traces = expand_dirs(raw)
        if traces and all(is_pinsight_trace(d) for d in traces):
            return 0                          # compatible: emit nothing
        if not traces and any(not os.path.isdir(d) for d in raw):
            # A non-directory argument we could not expand — typically a TC
            # EXPERIMENT name the wrapper did not resolve to member folders.
            # We cannot inspect it here, so stay lenient (compatible) rather
            # than silently greying every experiment out; the genuine
            # per-trace check still happens when the analysis is run.
            return 0
        # A real directory that is not a PInsight trace (e.g. a kernel or
        # other CTF/LTTng trace): report incompatible the way TC reads it.
        print(json.dumps({"error-message":
              "not a PInsight trace: no pinsight_lttng_ust provider in the "
              "trace metadata"}))
        return 0                              # exit code is irrelevant to TC

    dirs = expand_dirs(raw)
    if not dirs:
        print(usage); return 1
    if lami:
        b = epoch_ns_to_local_tod_ns(begin) if begin is not None else None
        e = epoch_ns_to_local_tod_ns(end)   if end   is not None else None
        tables_rows, span = analyze_lami(dirs, b, e)
        print(json.dumps(results(tables_rows, span, begin, end)))
    else:
        main_text(dirs)
    return 0
