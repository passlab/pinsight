#!/usr/bin/env python3
# Resolve a TraceCompass EXPERIMENT name to its member CTF trace dirs.
#
#   usage: resolve_experiment.py <workspace_dir> <experiment_name>
#          (prints one member trace dir per line; nothing if unresolved)
#
# TMF stores an experiment's membership as a SHADOW directory tree under
# <project>/Experiments/<name>/ that reproduces, relative to that folder, the
# path of each member trace under <project>/Traces/ (the leaf is a 0-byte
# marker whose name is the trace, e.g. .../64-bit). So we map every node of
# the shadow tree back to <project>/Traces/<relpath> and keep the ones that
# are real CTF traces (contain a `metadata` file). Robust to how TMF renders
# the leaf (0-byte file, dir, or link); needs no `.project` parsing (that file
# only records lazily-created .bookmarks, NOT experiment membership).
#
# A standalone script (not a heredoc in tc-common.sh) because bash 3.2 --
# macOS's default shell -- cannot parse a heredoc inside <(...) process
# substitution, and its parse error on stderr is merged into the analysis
# output by TraceCompass, striking the analysis out.
import os, sys, glob


def resolve(workspace, name):
    found = set()
    for proj in glob.glob(os.path.join(os.path.expanduser(workspace), "*")):
        exp = os.path.join(proj, "Experiments", name)
        traces = os.path.join(proj, "Traces")
        if not (os.path.isdir(exp) and os.path.isdir(traces)):
            continue
        for root, subdirs, files in os.walk(exp):
            for entry in files + subdirs:
                rel = os.path.relpath(os.path.join(root, entry), exp)
                real = os.path.join(traces, rel)
                if os.path.isfile(os.path.join(real, "metadata")):
                    found.add(real)
    return sorted(found)


if __name__ == "__main__":
    if len(sys.argv) != 3:
        print("usage: resolve_experiment.py <workspace_dir> <experiment_name>",
              file=sys.stderr)
        sys.exit(2)
    for d in resolve(sys.argv[1], sys.argv[2]):
        print(d)
