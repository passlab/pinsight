"""
PInsight Python Rate-Control Test Script

Provides three functions called many times to exercise:
  - max_num_traces  (hot_loop)
  - tracing_rate    (sampled_work)
  - window_end_action (once_then_stop)

Usage (via pinsight launcher):
  python3 -m pinsight test_rate_control.py [N]
  N = number of iterations (default 100)
"""
import sys
import time

N = int(sys.argv[1]) if len(sys.argv) > 1 else 100

def hot_loop(i):
    """Called N times. max_num_traces test: verify exactly K traces appear."""
    pass

def sampled_work(i):
    """Called N times. tracing_rate test: verify ~N/rate traces appear."""
    pass

def once_then_stop(i):
    """Called N times. window_end_action test: verify traces stop after K calls."""
    pass

for i in range(N):
    hot_loop(i)

for i in range(N):
    sampled_work(i)

for i in range(N):
    once_then_stop(i)
