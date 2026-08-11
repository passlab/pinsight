# PInsight User Guide

User documentation lives in [`user/`](user/):

- [trace_config_format.md](user/trace_config_format.md) — **the config-file specification** (start here)
- [domain_trace_modes.md](user/domain_trace_modes.md) — trace modes (OFF/STANDBY/MONITORING/TRACING) per domain
- [rate-limit-tracing.md](user/rate-limit-tracing.md) — rate control (max_num_traces & friends)
- [manifest.md](user/manifest.md) — the manifest event (run provenance and identity)
- [scheduler_launching.md](user/scheduler_launching.md) — running under Flux/Slurm (per-node sessions, multi-node recipes, GPU binding)
- [pinsight_feature_summary.md](user/pinsight_feature_summary.md) — feature overview
- [pinsight_region_begin.md](user/pinsight_region_begin.md) — user region API
- [python_trace_config.md](user/python_trace_config.md) — Python tracing configuration
- Trace analysis and TraceCompass integration: see [`../analysis/`](../analysis/)
  (its README is the analysis user guide)

Design documents, implementation plans, evaluation campaigns, and paper
material are maintained in the private `pinsight-eval` repository
(`docs/design/`); TraceCompass plugin development lives in
`pinsight-tracecompass`.
