"""
pinsight.py — Python launcher for PInsight sys.monitoring tracing.

Usage:
    python3 -m pinsight <your_script.py> [args...]

This module:
  1. Imports the _pinsight_python C extension (pysysmon_callback.c)
  2. Registers sys.monitoring callbacks for PY_START, PY_RETURN, CALL, C_RETURN
  3. Executes the target script via runpy.run_path()
  4. Deactivates tracing on exit

Requires Python 3.12+ (PEP 669 sys.monitoring).
"""
import sys
import runpy
import os

# Helper to find the compiled C-extension in the build directory during development.
# In a fully installed environment, this wouldn't be necessary.
__current_dir = os.path.dirname(os.path.abspath(__file__))
__build_dir = os.path.join(__current_dir, '..', 'build')
if os.path.exists(os.path.join(__build_dir, '_pinsight_python.so')):
    sys.path.insert(0, __build_dir)
elif os.path.exists(os.path.join(__current_dir, '_pinsight_python.so')):
    sys.path.insert(0, __current_dir)

try:
    import _pinsight_python
except ImportError as e:
    print(f"PInsight Error: Failed to import _pinsight_python C-extension: {e}")
    print("Make sure you have compiled PInsight with -DPINSIGHT_PYTHON=TRUE.")
    sys.exit(1)

TOOL_ID = 3
_PINSIGHT_TRACE_MODE_OFF = 1  # matches domain_trace_mode_t

# Set when activate() registered sys.monitoring callbacks (the EVENT
# instrument); the SAMPLE instrument is independent (two-instrument rule).
_callbacks_registered = False

def activate(events=("function", "c_call")):
    """
    Activates PInsight's two instruments independently (hybrid design REV 10):

      EVENT instrument  — sys.monitoring callbacks, registered only when the
                          configured trace_mode is above OFF. A pure-sampler
                          config ([Python.tracing] trace_mode = OFF) never
                          registers callbacks and pays zero dispatch.
      SAMPLE instrument — the stack probe is always registered (bookkeeping
                          only); its thread spawns lazily iff
                          [Python.sampling] interval_us > 0, now or after a
                          SIGUSR1/window reload.

    Args:
        events: tuple of event categories to enable.
                "function" enables PY_START + PY_RETURN (Python function tracing).
                "c_call" enables CALL + C_RETURN (C extension call tracing).
                () disables callback registration regardless of trace_mode.
    """
    global _callbacks_registered
    intended_mode, sampling_interval_us = _pinsight_python.get_config()

    # SAMPLE instrument: register the probe (lazy; independent of modes).
    _pinsight_python.start_sampler()

    # EVENT instrument: only when the configured mode is above OFF.
    if intended_mode <= _PINSIGHT_TRACE_MODE_OFF or not events:
        return

    _callbacks_registered = True
    sys.monitoring.use_tool_id(TOOL_ID, "pinsight")
    event_mask = 0

    if "function" in events:
        sys.monitoring.register_callback(
            TOOL_ID, sys.monitoring.events.PY_START,
            _pinsight_python.on_py_start)
        sys.monitoring.register_callback(
            TOOL_ID, sys.monitoring.events.PY_RETURN,
            _pinsight_python.on_py_return)
        event_mask |= sys.monitoring.events.PY_START
        event_mask |= sys.monitoring.events.PY_RETURN

    if "c_call" in events:
        sys.monitoring.register_callback(
            TOOL_ID, sys.monitoring.events.CALL,
            _pinsight_python.on_c_start)
        sys.monitoring.register_callback(
            TOOL_ID, sys.monitoring.events.C_RETURN,
            _pinsight_python.on_c_return)
        event_mask |= sys.monitoring.events.CALL
        # C_RETURN and C_RAISE are "dependent" events in Python 3.12+:
        # they fire automatically when CALL is enabled and a callback is
        # registered. They must NOT be set explicitly in the event mask
        # (doing so raises ValueError on Python 3.14+).

    sys.monitoring.set_events(TOOL_ID, event_mask)

    # Open the tracing window: restore the Python domain to the mode specified
    # by config file or env (saved in last_mode during library init).  The
    # domain was held in STANDBY until now so stdlib imports did not consume
    # per-thread lexgion cache slots.
    _pinsight_python.set_trace_mode()

def deactivate():
    """
    Deactivates PInsight tracing/sampling for Python.
    """
    # Stop the sampler FIRST: the framework joins the sampler thread, which
    # must happen before any interpreter teardown (the probe takes the GIL).
    _pinsight_python.stop_sampler()

    if not _callbacks_registered:
        return

    # Return the domain to STANDBY before tearing down callbacks.
    _pinsight_python.reset_trace_mode()
    # Unregister dependent-event callbacks before clearing the event mask.
    # Python 3.14+ raises ValueError if C_RETURN/C_RAISE are still registered
    # when set_events(TOOL_ID, 0) is called.
    sys.monitoring.register_callback(TOOL_ID, sys.monitoring.events.C_RETURN, None)
    sys.monitoring.register_callback(TOOL_ID, sys.monitoring.events.C_RAISE,  None)
    sys.monitoring.set_events(TOOL_ID, 0)
    sys.monitoring.free_tool_id(TOOL_ID)

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: python3 -m pinsight <your_script.py> [args...]")
        sys.exit(1)

    # 1. Pop 'pinsight' and shift sys.argv so the target script gets the correct arguments
    script_name = sys.argv[1]
    sys.argv = sys.argv[1:]

    # 2. Activate tracing implicitly
    print(f"[PInsight] Tracing activated for: {script_name}")
    activate()

    try:
        # 3. Execute the user's script as if it was run directly
        runpy.run_path(script_name, run_name="__main__")
    finally:
        # 4. Deactivate on exit
        deactivate()
        print("[PInsight] Tracing deactivated")
