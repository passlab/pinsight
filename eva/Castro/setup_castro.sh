#!/bin/bash
# -------------------------------------------------------------------
# Setup Castro source for PInsight evaluation
#
# Clones the upstream Castro repository at the exact commit used for
# evaluation, applies local compatibility patches, and copies the
# custom evaluation input files into the build tree.
#
# Usage:  ./setup_castro.sh            # clone + patch + copy inputs
#         ./setup_castro.sh --clean    # remove source/ and start fresh
# -------------------------------------------------------------------
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
SOURCE_DIR="$SCRIPT_DIR/source"
CASTRO_REPO="https://github.com/AMReX-Astro/Castro.git"
CASTRO_COMMIT="e5cc3f72fe68a559fd4ce565db61fd811ac5da95"

# --clean: remove existing checkout
if [ "${1:-}" = "--clean" ]; then
    echo "Removing $SOURCE_DIR ..."
    rm -rf "$SOURCE_DIR"
    echo "Cleaned.  Re-run without --clean to clone again."
    exit 0
fi

# Guard against re-cloning
if [ -d "$SOURCE_DIR/.git" ]; then
    echo "Castro already cloned in $SOURCE_DIR"
    echo "  commit: $(git -C "$SOURCE_DIR" rev-parse HEAD)"
    echo "  Use --clean to remove and re-clone."
    exit 0
fi

echo "=== Cloning Castro ==="
git clone "$CASTRO_REPO" "$SOURCE_DIR"
cd "$SOURCE_DIR"
git checkout "$CASTRO_COMMIT"

echo "=== Initializing submodules (AMReX, Microphysics) ==="
git submodule update --init --recursive

echo "=== Applying local patches ==="
for patch in "$SCRIPT_DIR"/patches/*.patch; do
    if [ -f "$patch" ]; then
        echo "  Applying $(basename "$patch") ..."
        git apply "$patch"
    fi
done

echo "=== Copying evaluation input files ==="
SEDOV_DIR="$SOURCE_DIR/Exec/hydro_tests/Sedov"
for input in "$SCRIPT_DIR"/inputs/inputs.*; do
    if [ -f "$input" ]; then
        cp "$input" "$SEDOV_DIR/"
        echo "  Copied $(basename "$input") → Sedov/"
    fi
done

echo ""
echo "=== Setup complete ==="
echo "Build Castro Sedov 3D with:"
echo "  cd $SEDOV_DIR"
echo "  make -j\$(nproc)"
echo ""
echo "Then run evaluation scripts from:"
echo "  $SCRIPT_DIR/scripts/"
