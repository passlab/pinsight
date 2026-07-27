# PInsight Documentation

## For users (`user/`)
- [trace_config_format.md](user/trace_config_format.md) — **the config-file specification** (start here)
- [domain_trace_modes.md](user/domain_trace_modes.md) — trace modes (OFF/STANDBY/MONITORING/TRACING) per domain
- [rate-limit-tracing.md](user/rate-limit-tracing.md) — rate control (max_num_traces & friends)
- [pinsight_feature_summary.md](user/pinsight_feature_summary.md) — feature overview
- [pinsight_region_begin.md](user/pinsight_region_begin.md) — user region API
- [python_trace_config.md](user/python_trace_config.md) — Python tracing configuration
- Analysis scripts: see `../analysis/` (mpi_jitter, load_imbalance, …, TraceCompass adapters)

## For developers (`design/`)
Design documents and implementation plans, kept as historical records — each carries a
Status header; superseded documents point at their successor rather than being rewritten.

Evaluation campaigns, results, and paper material live in the private `pinsight-eval` repo.
TraceCompass plugin development lives in `pinsight-tracecompass`.
