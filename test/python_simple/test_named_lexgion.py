"""
PInsight Phase 4 — Named Lexgion Workload

Provides functions exercising named lexgion config matching:
  - compute()           plain function  (co_qualname = 'compute')
  - preprocess()        plain function  (co_qualname = 'preprocess')
  - postprocess()       plain function  (co_qualname = 'postprocess')
  - Solver.run()        class method    (co_qualname = 'Solver.run')
  - Solver.setup()      class method    (co_qualname = 'Solver.setup')

Note: helper module functions are NOT called here to avoid qualname
collisions in basic tests. See test_phase4.sh 4.4 for the
filename-disambiguation test which uses an inline script.

Usage (via pinsight launcher):
  python3 -m pinsight test_named_lexgion.py [N]
  N = number of iterations per function (default 50)
"""
import sys

N = int(sys.argv[1]) if len(sys.argv) > 1 else 50


# ── Plain functions ──────────────────────────────────────────────────────────

def compute(i):
    """Named lexgion target — co_qualname = 'compute'."""
    pass

def preprocess(i):
    """Should fall through to domain default config."""
    pass

def postprocess(i):
    """Should fall through to domain default config."""
    pass


# ── Class method target ──────────────────────────────────────────────────────

class Solver:
    def run(self, i):
        """co_qualname = 'Solver.run'"""
        pass

    def setup(self, i):
        """co_qualname = 'Solver.setup' — should not be named-matched."""
        pass


# ── Execution ────────────────────────────────────────────────────────────────

solver = Solver()

for i in range(N):
    compute(i)

for i in range(N):
    preprocess(i)

for i in range(N):
    postprocess(i)

for i in range(N):
    solver.run(i)

for i in range(N):
    solver.setup(i)
