"""
PInsight Phase 5 — Thread/Punit Filtering Workload

Provides two function classes for per-thread config filtering tests:
  - main_work(i)   called only by the main thread  (pinsight thread ID 0)
  - worker(i)      called by NWORKERS explicit threads (IDs 1-NWORKERS)

Thread-ID assignment (pysysmon_get_thread_id):
  Assigned on first on_py_start call per thread, 0-based.
  Main thread always gets ID 0 (calls main_work first, before workers start).
  Each threading.Thread gets the next available ID (1, 2, ..., NWORKERS).

Usage (via pinsight launcher):
  python3 -m pinsight test_thread_filter.py [N [NWORKERS]]
  N        = iterations per function per thread (default 30)
  NWORKERS = number of threading.Thread workers (default 4)
"""
import sys
import threading

N        = int(sys.argv[1]) if len(sys.argv) > 1 else 30
NWORKERS = int(sys.argv[2]) if len(sys.argv) > 2 else 4


# ── Functions ────────────────────────────────────────────────────────────────

def main_work(i):
    """Called only by the main thread (pinsight thread 0)."""
    pass

def worker(i):
    """Called by each threading.Thread worker (pinsight thread IDs 1-NWORKERS)."""
    pass


# ── Execution ────────────────────────────────────────────────────────────────

# Main thread (ID 0) calls main_work BEFORE starting any worker threads.
# This guarantees the main thread is the first to call on_py_start and
# therefore gets pinsight thread ID 0.
for i in range(N):
    main_work(i)


def run_worker(_):
    """Each threading.Thread target: calls worker(i) N times."""
    for i in range(N):
        worker(i)


# Start NWORKERS independent threads.  Each threading.Thread is a distinct
# OS thread that receives a unique pinsight thread ID (1, 2, ..., NWORKERS).
threads = [threading.Thread(target=run_worker, args=(None,))
           for _ in range(NWORKERS)]
for t in threads:
    t.start()
for t in threads:
    t.join()
