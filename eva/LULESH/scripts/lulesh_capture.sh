#!/bin/bash
# Wrapper: runs command and tees output to a capture file
# Usage: lulesh_capture.sh <capture_file> <command...>
CAPTURE_FILE=$1; shift
"$@" 2>&1 | tee "$CAPTURE_FILE"
