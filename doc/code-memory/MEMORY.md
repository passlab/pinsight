# PInsight Memory Index

- [Project Overview](project_overview.md) — What PInsight is, its goals, and SC'26 paper context
- [Architecture](architecture.md) — Core data structures, modules, and component relationships
- [User Profile](user_profile.md) — Yonghong Yan, PInsight author and HPC researcher
- [Python Support](python_support.md) — Phases 1-5 complete on hip-rocm-support (02f85bc); STANDBY startup fix, named lexgion, punit filter, test suite; next: Python+HIP cross-domain
- [HIP/ROCm Support](hip_rocm_support.md) — Phase 1-3 on branch hip-rocm-support (4cd22bd); next: build on El Capitan, fix ROCTracer field names, rocprofiler-sdk counters
- [Tuolumne Build](tuolumne_build.md) — LTTng 2.13 built in ~/local; GCC for libpinsight, clang -fopenmp for traced apps (libgomp kills OMPT)
