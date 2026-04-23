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
    print("Make sure you have compiled PInsight with Python support.")
    sys.exit(1)

TOOL_ID = 3

def activate(events=("function", "c_call")):
    """
    Activates LTTng tracing for Python using sys.monitoring.
    """
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
            _pinsight_python.on_call)
        event_mask |= sys.monitoring.events.CALL

    sys.monitoring.set_events(TOOL_ID, event_mask)

def deactivate():
    """
    Deactivates LTTng tracing for Python.
    """
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
