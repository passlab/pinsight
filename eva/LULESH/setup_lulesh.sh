#!/bin/bash
# -------------------------------------------------------------------
# Setup LULESH source for PInsight evaluation
#
# Clones the LULESH repository at the exact commit used for evaluation
# and applies the local PInsight patch (build-system changes + the
# knob-controlled per-region num_threads instrumentation in lulesh.cc).
#
# LULESH takes its workload on the command line (-s <size> -i <iters>),
# so there are no input files to copy.
#
# Usage:  ./setup_lulesh.sh            # clone + patch
#         ./setup_lulesh.sh --clean    # remove source/ and start fresh
# -------------------------------------------------------------------
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
SOURCE_DIR="$SCRIPT_DIR/source"
LULESH_REPO="https://github.com/passlab/LULESH.git"
# Clean upstream base (last real LLNL merge, before any PInsight commits).
# The PInsight source modifications are carried by patches/ instead.
LULESH_COMMIT="3e01c40"

# --clean: remove existing checkout
if [ "${1:-}" = "--clean" ]; then
    echo "Removing $SOURCE_DIR ..."
    rm -rf "$SOURCE_DIR"
    echo "Cleaned.  Re-run without --clean to clone again."
    exit 0
fi

# Guard against re-cloning
if [ -d "$SOURCE_DIR/.git" ]; then
    echo "LULESH already cloned in $SOURCE_DIR"
    echo "  commit: $(git -C "$SOURCE_DIR" rev-parse HEAD)"
    echo "  Use --clean to remove and re-clone."
    exit 0
fi

echo "=== Cloning LULESH ==="
git clone "$LULESH_REPO" "$SOURCE_DIR"
cd "$SOURCE_DIR"
git checkout "$LULESH_COMMIT"

echo "=== Applying local patches ==="
for patch in "$SCRIPT_DIR"/patches/*.patch; do
    if [ -f "$patch" ]; then
        echo "  Applying $(basename "$patch") ..."
        git apply "$patch"
    fi
done

echo ""
echo "=== Setup complete ==="
echo "Build LULESH with:"
echo "  cd $SOURCE_DIR"
echo "  make -j\$(nproc)          # produces lulesh2.0"
echo ""
echo "The Makefile links against \$(PINSIGHT_ROOT)/src/app_knob.o for the knob"
echo "instrumentation and defaults to clang++-21; adjust SERCXX/MPICXX in the"
echo "Makefile for your machine's compilers if needed."
echo ""
echo "Then run evaluation scripts from:"
echo "  $SCRIPT_DIR/scripts/"
