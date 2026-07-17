# PInsight Memory Index

- [Project Overview](project_overview.md) — What PInsight is, its goals, and SC'26 paper context
- [Architecture](architecture.md) — Core data structures, modules, and component relationships
- [User Profile](user_profile.md) — Yonghong Yan, PInsight author and HPC researcher
- [Python Support](python_support.md) — Phases 1-5 complete on hip-rocm-support (02f85bc); STANDBY startup fix, named lexgion, punit filter, test suite; next: Python+HIP cross-domain
- [HIP/ROCm Support](hip_rocm_support.md) — built + trace-validated on Tuolumne (4× MI300A, 2026-06-10); gotchas: LTTng provider prio 150, lazy clock calibration, pool needs no context
- [Tuolumne Build](tuolumne_build.md) — LTTng 2.13 built in ~/local; GCC for libpinsight, clang -fopenmp for traced apps (libgomp kills OMPT)
- [Energy/Power Support](energy_power_support.md) — pluggable backends; AMD-SMI is the working MI300A path (RAPL root-locked, Variorum unsupported); AMD-SMI teardown gotcha → exit read from control thread; Feature 1 done, Feature 2/config TODO
- [Streaming TraceCompass Client](streaming_tracecompass_client.md) — laptop→Tuolumne tunnel topology; Eclipse JSch fork swap + PREF_KEX_METHODS fix; live-demo producer recipe
- [Analysis Toolkit](analysis_toolkit.md) — peam/src/python structured scripts (2026-07-17): pinsight_reader + load-imbalance/MPI-latency/GPU-datamovement/halo-exchange; first 4-node findings
- [AMG2023 Eval & Overhead](amg2023_eval_overhead.md) — parser ignores `[HIP(subdomain).default]` (use single `[HIP.default]`); default=unlimited tracing; overhead full vs rate50 vs notrace; multi-node SUCCESS 2026-07-16 (4 nodes/16 GPUs) — LTTNG_HOME must be node-local AND set for the traced app, session lifecycle must share the flux-run task that runs the app
