# mode_window — GPU-free test for `window_timeout` + `mode_after_trigger`

Exercises the **domain-agnostic** core of the tracing-window feature (the
control-thread wall-clock timer and the `all` count-policy gate) via OpenMP/OMPT,
so it runs on any node — **no GPU, no flux, no LTTng session** required. The
auto-trigger logic runs off per-region trace counters regardless of any trace
sink, so the test asserts on PInsight's stderr control messages.

```bash
./window_all_test.sh          # builds mode_test.c (clang-21 -fopenmp) and runs 6 cases
# env: PINSIGHT_LIB=<…/build_omp/libpinsight.so>  OMP_LIB=<…/libomp.so>
```

Cases: `first` fires on the fast region (early) vs `all` waits for the slow region
(late); `window_timeout` standalone; `window_timeout` as the `all` never-fires
backstop; count-wins-the-race → timer no-ops (regression for the cross-config
arbiter fix, see `doc/mode_after_timeout_design.md` §10); and the `all`-without-
timeout startup warning.

The HIP end-to-end counterpart (needs an MI300A node) is
`test/rocm/window_timeout_test.sh` (`make window`).
