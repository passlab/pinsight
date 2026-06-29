# Changelog

## Unreleased

### Added
- **`window_timeout`** — a per-process wall-clock deadline that ends the current
  TRACING window and performs `trace_mode_after` (mode switch *or* INTROSPECT)
  even if no region reaches `max_num_traces`. Config key
  `[Lexgion.default] window_timeout = N` (integer seconds; `0`/absent = disabled).
  Implemented entirely in the control thread (zero application hot-path cost);
  the clock starts at process start and re-arms each cyclic window. Usable
  standalone as time-windowed capture, or as a guaranteed ceiling on TRACING
  duration. See `doc/mode_after_timeout_design.md`.
- **`mode_after_trigger`** count policy — `first` (default) or `all`. `all` fires
  only once every region a thread has seen has capped (per-thread; first thread to
  satisfy it fires). `anchor` is reserved/not yet implemented (parser rejects it,
  falls back to `first`). PInsight warns at startup if `all` is set without a
  `window_timeout` backstop (the never-fires case).
- New tests: parser cases WT1–WT7 in `test/trace_config_parse`; HIP integration
  `test/rocm/window_timeout_test.sh` (`make window`).

### Changed (⚠ breaking)
- Environment variable **`PINSIGHT_TRACE_RATE` renamed to `PINSIGHT_TRACE_WINDOW`**,
  and its grammar gains a 4th positional field:
  `start:max:rate:window_timeout[:mode_after_string]` (was
  `start:max:rate[:mode_after_string]`). `PINSIGHT_TRACE_RATE` is retained as a
  **deprecated alias** (same new grammar) and prints a one-line warning.
  **Migration:** any value that used the old 4th field (e.g.
  `0:50:1:HIP:MONITORING`) must insert the new field
  (`0:50:1:0:HIP:MONITORING`).

### Renamed (internal)
- `trace_mode_after_t.introspect_timeout` → `introspect_pause_duration` (clarifies
  it is the INTROSPECT pause length, distinct from `window_timeout`). The
  INTROSPECT config/env string grammar (`INTROSPECT:<pause>:<script>[:<mode>]`) is
  unchanged.
