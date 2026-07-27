# Signal-Based Config Reload: Design History

> [!NOTE]
> This document describes the **original design** for signal-based config reload.
> This approach has been **superseded** by the dedicated PInsight Control Thread.
> See [control_thread_design.md](control_thread_design.md) for the current architecture.

## Original Approach: Flag-Based Deferred Reconfig

Signal handlers have strict restrictions — only async-signal-safe functions are allowed.
The handler sets a flag, and the actual config re-read happens at the next callback safe point:

```c
static volatile sig_atomic_t config_reload_requested = 0;
static void pinsight_signal_handler(int sig) {
    config_reload_requested = 1;  // Only safe operation in a signal handler
}
```

Then check the flag at `parallel_begin` (or similar callback):
```c
if (__atomic_exchange_n(&config_reload_requested, 0, __ATOMIC_SEQ_CST)) {
    pinsight_load_trace_config(NULL);
}
```

**Problems**: Every callback checked this flag (wasted cycles), each domain had its own
deferred-reconfig block (~106 lines of duplicated logic), and the OFF→TRACING transition
required calling non-signal-safe OMPT functions from the signal handler.

## Current Approach: Control Thread

The signal handler is now trivial:
```c
static void pinsight_sigusr1_handler(int sig) {
    __atomic_or_fetch(&pending_wakeup_reason, PINSIGHT_WAKEUP_CONFIG_RELOAD, __ATOMIC_SEQ_CST);
    sem_post(&control_sem);  // async-signal-safe
}
```

The dedicated control thread wakes from `sem_wait()`, performs the config reload, and
applies mode changes. No flags are checked in the callback hot path. See
[control_thread_design.md](control_thread_design.md) for details.