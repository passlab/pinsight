#!/usr/bin/env bash
# run_pinsight_warpx.sh
# Automated build, setup, and run script for WarpX with PInsight on 4 GPUs.

set -euo pipefail

# 1. Paths and Checks
PINSIGHT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$PINSIGHT_ROOT"

# Find a suitable python interpreter (must be >= 3.12 for PInsight PEP 669 tracing and pyAMReX >=3.11 requirements)
find_python() {
    for py in python3.13 python3.12 python3; do
        if command -v "$py" &>/dev/null; then
            local ver
            ver=$("$py" -c "import sys; print(f'{sys.version_info.major}.{sys.version_info.minor}')")
            local major=$(echo "$ver" | cut -d. -f1)
            local minor=$(echo "$ver" | cut -d. -f2)
            if [ "$major" -eq 3 ] && [ "$minor" -ge 12 ]; then
                echo "$py"
                return 0
            fi
        fi
    done
    return 1
}

if [ -d "venv" ]; then
    # Check if the existing venv is using Python >= 3.12
    local_py_ver=$(./venv/bin/python3 -c "import sys; print(sys.version_info.minor)" 2>/dev/null || echo "0")
    if [ "$local_py_ver" -lt 12 ]; then
        echo "[Setup] Existing venv is outdated (Python 3.${local_py_ver} < 3.12). Deleting to recreate..."
        rm -rf venv
    fi
fi

if [ ! -d "venv" ]; then
    if ! PY_BIN=$(find_python); then
        echo "Error: PInsight Python tracing requires Python 3.12+ (due to CPython sys.monitoring/PEP 669 requirements)."
        echo "Please install Python 3.12+ (e.g., 'sudo apt install python3.12 python3.12-venv python3.12-dev') and run this script again."
        exit 1
    fi
    echo "[Setup] Creating Python virtual environment (venv) using $PY_BIN..."
    "$PY_BIN" -m venv venv
fi

echo "[Setup] Activating Python virtual environment..."
# Temporarily disable unbound variable checking because venv activation script references unset variables
set +u
source venv/bin/activate
set -u

echo "[Setup] Ensuring pip and basic build tools are updated inside the venv..."
pip install --upgrade pip setuptools wheel

echo "[Setup] Installing CMake and WarpX python dependencies inside the venv..."
pip install cmake
if [ -d "warpx" ] && [ -f "warpx/requirements.txt" ]; then
    pip install -r warpx/requirements.txt
fi
pip install mpi4py


# Dynamically query the virtual environment's python paths to pass to CMake.
# This prevents FindPython/FindPython3 from failing inside virtual environments.
PY_EXE="$PINSIGHT_ROOT/venv/bin/python3"
PY_INC=$("$PY_EXE" -c "import sysconfig; print(sysconfig.get_path('include'))")
PY_LIB=$("$PY_EXE" -c "import sysconfig; import os; print(os.path.join(sysconfig.get_config_var('LIBDIR'), sysconfig.get_config_var('LDLIBRARY')))")

echo "========================================================="
echo "Starting WarpX + PInsight 4-GPU Run Setup"
echo "========================================================="

# 2. Build PInsight with Python, CUDA, MPI, and OpenMP domains
echo "[Build] Compiling PInsight..."
mkdir -p build
cd build
cmake -DCMAKE_BUILD_TYPE=Release \
      -DPINSIGHT_PYTHON=TRUE \
      -DPINSIGHT_CUDA=TRUE \
      -DPINSIGHT_MPI=TRUE \
      -DPINSIGHT_OPENMP=TRUE \
      -DPython3_EXECUTABLE="$PY_EXE" \
      -DPython3_INCLUDE_DIR="$PY_INC" \
      -DPython3_LIBRARY="$PY_LIB" \
      -DPython_EXECUTABLE="$PY_EXE" \
      -DPython_INCLUDE_DIR="$PY_INC" \
      -DPython_LIBRARY="$PY_LIB" ..
make -j"$(nproc)"
cd ..

# 3. Build WarpX C-Extension / Python bindings
if ! python3 -c "import pywarpx" 2>/dev/null; then
    # Ensure WarpX submodule/source is present on the VM
    if [ ! -f "warpx/CMakeLists.txt" ]; then
        echo "[Build] warpx/CMakeLists.txt not found! Cloning WarpX repository..."
        if [ -d "warpx" ]; then
            # Clean up the empty directory/link to avoid git clone failure
            rm -rf warpx
        fi
        git clone --recursive https://github.com/ECP-WarpX/warpx.git warpx
    fi

    echo "[Build] Compiling and installing WarpX Python package (PICMI)..."
    cd warpx
    mkdir -p build_py
    cd build_py
    cmake -S .. -B . \
          -DWarpX_DIMS="3" \
          -DWarpX_PYTHON=ON \
          -DWarpX_COMPUTE=CUDA \
          -DWarpX_MPI=ON \
          -DPython3_EXECUTABLE="$PY_EXE" \
          -DPython3_INCLUDE_DIR="$PY_INC" \
          -DPython3_LIBRARY="$PY_LIB" \
          -DPython_EXECUTABLE="$PY_EXE" \
          -DPython_INCLUDE_DIR="$PY_INC" \
          -DPython_LIBRARY="$PY_LIB"
    cmake --build . -j"$(nproc)" --target pip_install
    cd "$PINSIGHT_ROOT"
else
    echo "[Build] WarpX is already installed."
fi

# 4. Prepare Tracing Directories
mkdir -p traces

# 5. Start LTTng Daemon and Session
echo "[Trace] Setting up LTTng session..."
lttng-sessiond -d || true

SESSION_NAME="pinsight-warpx"
lttng destroy $SESSION_NAME 2>/dev/null || true
lttng create $SESSION_NAME --output="$PINSIGHT_ROOT/traces/warpx-trace"

# Enable targeted PInsight tracepoints
echo "[Trace] Enabling userspace event tracepoints..."
lttng enable-event --userspace "pysysmon_pinsight_lttng_ust:*"
lttng enable-event --userspace "cupti_pinsight_lttng_ust:*"
lttng enable-event --userspace "pmpi_pinsight_lttng_ust:*"
lttng enable-event --userspace "ompt_pinsight_lttng_ust:*"

lttng start

# 6. Configure Tracing Environment Variables
export PINSIGHT_TRACE_CONFIG_FILE="$PINSIGHT_ROOT/pinsight_trace_config.txt"
export LD_PRELOAD="$PINSIGHT_ROOT/build/libpinsight.so"
export PYTHONPATH="$PINSIGHT_ROOT/build:${PYTHONPATH:-}"

# AMReX/WarpX environment options
export OMP_NUM_THREADS=8  # Number of OpenMP threads per MPI rank
export AMREX_DEFAULT_INIT="amrex.use_gpu_aware_mpi=1"

# 7. Create local MPI rank to GPU binding wrapper
cat << 'EOF' > gpu_wrapper.sh
#!/usr/bin/env bash
# gpu_wrapper.sh: Maps local MPI rank index to CUDA_VISIBLE_DEVICES index on the node.

if [ -n "${OMPI_COMM_WORLD_LOCAL_RANK:-}" ]; then
    LOCAL_ID=$OMPI_COMM_WORLD_LOCAL_RANK
elif [ -n "${MPI_LOCALRANKID:-}" ]; then
    LOCAL_ID=$MPI_LOCALRANKID
elif [ -n "${SLURM_LOCALID:-}" ]; then
    LOCAL_ID=$SLURM_LOCALID
else
    LOCAL_ID=0
fi

# Explicitly bind local rank to respective GPU ID (0, 1, 2, or 3)
export CUDA_VISIBLE_DEVICES=$LOCAL_ID
echo "[Rank $LOCAL_ID] Selected GPU $CUDA_VISIBLE_DEVICES"

exec "$@"
EOF
chmod +x gpu_wrapper.sh

# 8. Execute simulation on 4 GPUs using python launcher
echo "========================================================="
echo "Launching 4-GPU WarpX Simulation under PInsight"
echo "========================================================="

# Note: We run with 4 MPI tasks (1 per GPU)
mpirun -np 4 ./gpu_wrapper.sh "$PINSIGHT_ROOT/venv/bin/python3" -m pinsight run_warpx_scaled.py

echo "========================================================="
echo "Simulation Finished. Stopping Trace Collection."
echo "========================================================="

# 9. Clean up and Stop Tracing
lttng stop

echo "[Trace] Tracing stopped. Traces successfully saved in ./traces/warpx-trace"
echo "[Trace] Detailed event counts:"
babeltrace2 "$PINSIGHT_ROOT/traces/warpx-trace" | wc -l || echo "Babeltrace2 not installed or failed to read traces."

echo "[Trace] Sample first 20 events:"
babeltrace2 "$PINSIGHT_ROOT/traces/warpx-trace" | head -n 20 || true

# Destroy session to release resources
lttng destroy $SESSION_NAME
rm -f gpu_wrapper.sh

echo "Done!"
