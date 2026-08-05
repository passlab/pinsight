/**
 * pinsight_control_thread.c
 *
 * Dedicated PInsight control thread — centralizes all configuration
 * changes using a single-writer / multiple-reader (SWMR) pattern.
 *
 * This thread:
 *   - Sleeps in sem_wait() consuming zero CPU when idle.
 *   - Wakes on SIGUSR1, auto-trigger, or inotify (future).
 *   - Reads config files, updates volatile mode flags.
 *   - Enables/disables CUPTI callbacks (process-global, thread-safe).
 *   - Registers/deregisters OMPT callbacks (tested safe from pthread).
 *   - Manages introspection pause/resume.
 *   - Updates application knobs.
 */
#include "pinsight_control_thread.h"
#include "manifest.h"
#include "pinsight.h"
#include "pinsight_config.h"
#include "trace_config.h"
#ifdef PINSIGHT_ENERGY
#include "energy.h"
#endif
#include <errno.h>
#include <pthread.h>
#include <semaphore.h>
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

/* environ is required for posix_spawn to pass the full environment */
extern char **environ;

/* ================================================================
 * Shared state — written only by control thread, read by app threads
 * ================================================================ */
volatile int pinsight_app_paused = 0;

/* Pause synchronization primitives */
pthread_mutex_t pinsight_pause_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t  pinsight_pause_cond  = PTHREAD_COND_INITIALIZER;

/* ================================================================
 * Control thread internals
 * ================================================================ */
static pthread_t control_thread;
static sem_t control_sem;
static volatile int control_shutdown = 0;

/* Pending wakeup reason — written by wakeup callers, read by control thread.
 * Multiple wakeups can OR their reasons together. */
static volatile int pending_wakeup_reason = 0;

/* Pending mode action — set by auto-trigger, consumed by control thread */
static window_end_action_t *pending_mode_action = NULL;

/* ----------------------------------------------------------------
 * window_timeout: a single per-process wall-clock deadline that ends the
 * current TRACING window even if no region reaches max_num_traces. State is
 * file-scope and touched ONLY by the control thread (no locking). The timer
 * drives off the default lexgion's end_action (the per-process window config).
 * ---------------------------------------------------------------- */
static int                 window_timer_armed = 0;
static struct timespec     window_deadline;        /* CLOCK_REALTIME */
static window_end_action_t *window_timer_ma = NULL; /* action fired on ETIMEDOUT */

/* True if the window's action has ALREADY been applied this window (i.e. a count
 * policy ended the window first), so the timer must NOT re-fire it.
 *
 * Why not just rely on ma->fired: count triggers fire off the per-domain *default
 * copies* of the window's end_action (lexgions resolve to
 * lexgion_domain_default_trace_config[domain], a copy of lexgion_default), so the
 * timer's ma->fired (on lexgion_default) is a DIFFERENT latch and would not see
 * the count fire. The domain-level mode_change_fired latch, by contrast, is set by
 * BOTH the count immediate-path (pinsight.c) and the control-thread path — it is
 * the real cross-config arbiter. Returns true only when the action targets at
 * least one domain and every targeted domain has already switched. (Pure
 * INTROSPECT with no resume modes has no domain target; that narrow count+timer
 * race is not arbitrated here — see end_action_timeout_design.md §9.) */
static int window_already_ended(window_end_action_t *ma) {
    int has_target = 0;
    for (int d = 0; d < num_domain; d++) {
        if (ma->mode[d] == PINSIGHT_DOMAIN_NONE)
            continue;
        has_target = 1;
        if (!domain_default_trace_config[d].mode_change_fired)
            return 0; /* a targeted domain has not switched yet */
    }
    return has_target;
}

/* ----------------------------------------------------------------
 * Periodic manifest timer (WS1 Step 3). State touched ONLY by the control
 * thread. Dormancy rule (ws1_manifest_design.md §2.4): the periodic burst
 * arms iff anything is being collected — any domain mode != OFF (the
 * CONFIGURED mode, uniform across ranks — never the per-rank elected role)
 * or energy measure != off — and [Manifest] interval > 0. Lifecycle bursts
 * (init/mpi_init/window/fini) are unconditional and emitted elsewhere.
 * ---------------------------------------------------------------- */
static int             manifest_timer_armed = 0;
static struct timespec manifest_deadline; /* CLOCK_REALTIME */

static int manifest_periodic_wanted(void) {
  if (manifest_interval_sec <= 0)
    return 0;
  if (energy_measure_policy != PINSIGHT_NODEPOLICY_OFF)
    return 1;
  for (int d = 0; d < num_domain; d++)
    if (domain_default_trace_config[d].mode != PINSIGHT_DOMAIN_OFF)
      return 1;
  return 0;
}

/* (Re)arm/disarm per the dormancy rule. Called at control-thread start, after
 * each periodic emission, and after every reload/mode transition (the
 * predicate inputs — modes, energy policy, interval — may all have changed). */
static void manifest_timer_arm_from_config(void) {
  if (manifest_periodic_wanted()) {
    clock_gettime(CLOCK_REALTIME, &manifest_deadline);
    manifest_deadline.tv_sec += manifest_interval_sec;
    manifest_timer_armed = 1;
  } else {
    manifest_timer_armed = 0;
  }
}

static int ts_before(const struct timespec *a, const struct timespec *b) {
  return a->tv_sec < b->tv_sec ||
         (a->tv_sec == b->tv_sec && a->tv_nsec < b->tv_nsec);
}

/* (Re)arm the window timer from the current default config. Called at control
 * thread start, after each config reload, and after each cyclic-window reset so
 * every TRACING window is bounded. window_timeout_sec <= 0 disarms. */
static void window_timer_arm_from_default(void) {
    window_end_action_t *ma = &lexgion_default_trace_config->end_action;
    if (ma->window_timeout_sec > 0) {
        clock_gettime(CLOCK_REALTIME, &window_deadline);
        window_deadline.tv_sec += ma->window_timeout_sec;
        window_timer_ma = ma;
        window_timer_armed = 1;
    } else {
        window_timer_ma = NULL;
        window_timer_armed = 0;
    }
}

/* ================================================================
 * Introspection support — moved from pinsight.c
 *
 * Implements the pause semantics:
 *   timeout > 0: pause for N seconds (interruptible by SIGUSR1)
 *   timeout = 0: no pause, just run script
 *   timeout = -1: pause indefinitely until SIGUSR1
 * ================================================================ */
static void control_execute_introspect(window_end_action_t *ma) {
    /* 1. Launch introspection script if configured */
    if (ma->introspect_script[0] && strcmp(ma->introspect_script, "-") != 0) {
        /* Build the LTTng chunk path for the script */
        char chunk_path[512] = "";
        char *trace_home = getenv("LTTNG_HOME");
        if (!trace_home) trace_home = getenv("HOME");
        if (trace_home) {
            snprintf(chunk_path, sizeof(chunk_path),
                     "%s/lttng-traces", trace_home);
        }

        /* Build argv for: bash <script> <chunk_path> <pid_str> <config> */
        char pid_str[32];
        snprintf(pid_str, sizeof(pid_str), "%d", getpid());
        char *config_file = getenv("PINSIGHT_TRACE_CONFIG_FILE");
        if (!config_file) config_file = "pinsight_trace_config.txt";

        char *argv[] = {
            "bash", (char *)ma->introspect_script,
            chunk_path, pid_str, config_file, NULL
        };

        /* Build a clean envp: copy environ but strip LD_PRELOAD and
         * OMP_TOOL_LIBRARIES to prevent TBB/OMPT from re-initialising
         * inside the bash child — a common cause of fork-child deadlocks. */
        int env_count = 0;
        while (environ[env_count]) env_count++;
        char **child_env = malloc((env_count + 1) * sizeof(char *));
        int j = 0;
        for (int i = 0; i < env_count; i++) {
            if (strncmp(environ[i], "LD_PRELOAD=", 11) == 0) continue;
            if (strncmp(environ[i], "OMP_TOOL_LIBRARIES=", 19) == 0) continue;
            child_env[j++] = environ[i];
        }
        child_env[j] = NULL;

        /* posix_spawn is safe to use from multithreaded context — it does
         * not inherit locked mutexes the way fork() does. */
        pid_t pid = -1;
        int rc = posix_spawn(&pid, "/bin/bash", NULL, NULL, argv, child_env);
        free(child_env);

        if (rc == 0) {
            fprintf(stderr, "PInsight: Launched analysis script '%s' (pid %d)\n",
                    ma->introspect_script, pid);
        } else {
            fprintf(stderr, "PInsight WARNING: posix_spawn failed for '%s': %s\n",
                    ma->introspect_script, strerror(rc));
        }
    }

    /* 2. Handle pause based on timeout */
    if (ma->introspect_pause_duration == 0) {
        /* No pause — just ran the script, continue */
        return;
    }

    /* Pause the application */
    pthread_mutex_lock(&pinsight_pause_mutex);
    pinsight_app_paused = 1;
    pthread_mutex_unlock(&pinsight_pause_mutex);
    fprintf(stderr, "PInsight: Application paused for introspection\n");

    if (ma->introspect_pause_duration > 0) {
        /* Timed wait — interruptible by SIGUSR1 → sem_post */
        struct timespec deadline;
        clock_gettime(CLOCK_REALTIME, &deadline);
        deadline.tv_sec += ma->introspect_pause_duration;

        int ret = sem_timedwait(&control_sem, &deadline);
        if (ret == -1 && errno == ETIMEDOUT) {
            fprintf(stderr, "PInsight: INTROSPECT timeout (%ds), auto-resuming\n",
                    ma->introspect_pause_duration);
        } else {
            fprintf(stderr, "PInsight: INTROSPECT woken by SIGUSR1, resuming\n");
        }
    } else {
        /* timeout == -1: indefinite wait — only SIGUSR1 resumes */
        fprintf(stderr, "PInsight: Waiting indefinitely for SIGUSR1 to resume\n");
        sem_wait(&control_sem);
        fprintf(stderr, "PInsight: INTROSPECT woken by SIGUSR1, resuming\n");
    }

    /* Resume the application */
    pthread_mutex_lock(&pinsight_pause_mutex);
    pinsight_app_paused = 0;
    pthread_cond_broadcast(&pinsight_pause_cond);
    pthread_mutex_unlock(&pinsight_pause_mutex);
    fprintf(stderr, "PInsight: Application resumed\n");
}

/* ================================================================
 * Apply mode changes to all domains (4-mode: OFF/STANDBY/MONITOR/TRACE)
 * ================================================================ */
static void control_apply_all_modes(void) {
#ifdef PINSIGHT_CUDA
    pinsight_control_cuda_apply_mode();
    /* Phase 2: a config reload may flip device_activity with NO mode change
     * (apply_mode early-returns on mode==last) — re-check the live collect
     * decision unconditionally. Idempotent, cheap. */
    pinsight_control_cuda_apply_collect_state();
#endif
#ifdef PINSIGHT_HIP
    pinsight_control_hip_apply_mode();
    pinsight_control_hip_apply_collect_state();
#endif
    /* OMPT note: calling ompt_set_callback from the control thread to
     * register/deregister callbacks while OpenMP regions are active has
     * not shown issues in practice (LLVM runtime uses simple pointer
     * stores for callback slots). The volatile mode flag in each callback
     * provides a safe fallback regardless.
     *
     * If crashes are observed during mode switches, disable this block
     * and rely solely on the volatile mode flag killswitch. */
#ifdef PINSIGHT_OPENMP
    // pinsight_control_openmp_apply_mode();
#endif
    /* MPI: no registration needed — PMPI wrappers read volatile mode flag */
}

/* ================================================================
 * Main control thread loop
 * ================================================================ */
static void *pinsight_control_loop(void *arg) {
    (void)arg;

    /* Block SIGUSR1 on the control thread — the signal should be
     * delivered to an app thread that runs our handler.
     * This prevents SIGUSR1 from interrupting sem_wait with EINTR. */
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGUSR1);
    pthread_sigmask(SIG_BLOCK, &mask, NULL);

    /* Arm the window timer at control-thread start (≈ process start): the
     * window_timeout clock is relative to here (design decision §7). */
    window_timer_arm_from_default();

    /* WS1 Step 3: arm the periodic manifest timer (dormancy-gated) and flush
     * the effective-config dump file once at start — the constructor's init
     * burst already published this config's hash (ws1 design §2.8: file I/O
     * happens only here and after reloads, only on this thread, and only
     * when PINSIGHT_MANIFEST_DIR is set). */
    manifest_timer_arm_from_config();
    pinsight_manifest_dump_config();

    while (!control_shutdown) {
        /* Block until signaled — zero CPU when idle. When a window timeout is
         * armed, block with a deadline instead so the window ends on time even
         * if no region caps. Restart on EINTR (defensive; SIGUSR1 is blocked). */
        int rc;
        /* Phase 2 (§6.9.5): if any GPU domain is live-rotating its activity
         * collector, wake exactly at the next rotation boundary (no fine
         * polling — one wake per period). Rotation boundaries are defined on
         * CLOCK_MONOTONIC (node-wide); sem_timedwait needs an absolute
         * CLOCK_REALTIME deadline, so convert the remaining time. When both a
         * rotation boundary and the window deadline are pending, wait on the
         * earlier one. rotate_ms == 0 for all non-rotate policies → this
         * branch is never taken (pure event-driven wait, zero polling). */
        int rotate_ms = 0;
#ifdef PINSIGHT_HIP
        {
            int t = pinsight_control_hip_rotate_period_ms();
            if (t > 0 && (rotate_ms == 0 || t < rotate_ms)) rotate_ms = t;
        }
#endif
#ifdef PINSIGHT_CUDA
        {
            int t = pinsight_control_cuda_rotate_period_ms();
            if (t > 0 && (rotate_ms == 0 || t < rotate_ms)) rotate_ms = t;
        }
#endif
        /* Wait on the EARLIEST armed deadline — rotation boundary, window
         * timeout, or periodic manifest — remembering which kind it was so
         * an ETIMEDOUT dispatches to the right handler. No deadline armed =>
         * pure event-driven sem_wait (zero polling; the dormancy rule keeps
         * the manifest timer disarmed when nothing is collected). */
        enum { WAKE_NONE, WAKE_ROTATE, WAKE_WINDOW, WAKE_MANIFEST } wake_kind =
            WAKE_NONE;
        struct timespec rotate_deadline, *earliest = NULL;
        if (rotate_ms > 0) {
            struct timespec mono, rt;
            clock_gettime(CLOCK_MONOTONIC, &mono);
            clock_gettime(CLOCK_REALTIME, &rt);
            unsigned long long mono_ms =
                (unsigned long long)mono.tv_sec * 1000ULL +
                (unsigned long long)(mono.tv_nsec / 1000000L);
            /* time to the next boundary, +2 ms so we land just after it */
            long rem_ms = (long)(rotate_ms - (long)(mono_ms % rotate_ms)) + 2;
            long ns = rt.tv_nsec + (rem_ms % 1000) * 1000000L;
            rotate_deadline.tv_sec  = rt.tv_sec + rem_ms / 1000 + ns / 1000000000L;
            rotate_deadline.tv_nsec = ns % 1000000000L;
            earliest = &rotate_deadline;
            wake_kind = WAKE_ROTATE;
        }
        if (window_timer_armed &&
            (!earliest || ts_before(&window_deadline, earliest))) {
            earliest = &window_deadline;
            wake_kind = WAKE_WINDOW;
        }
        if (manifest_timer_armed &&
            (!earliest || ts_before(&manifest_deadline, earliest))) {
            earliest = &manifest_deadline;
            wake_kind = WAKE_MANIFEST;
        }
        rc = earliest ? sem_timedwait(&control_sem, earliest)
                      : sem_wait(&control_sem);
        if (rc == -1 && errno == EINTR)
            continue;

        if (rc == -1 && errno == ETIMEDOUT && wake_kind == WAKE_ROTATE) {
            /* Rotation boundary: re-evaluate the live collect decision (the
             * outgoing collector closes its pool, the incoming one opens).
             * Loop back to recompute the next boundary. */
#ifdef PINSIGHT_HIP
            pinsight_control_hip_apply_collect_state();
#endif
#ifdef PINSIGHT_CUDA
            pinsight_control_cuda_apply_collect_state();
#endif
            continue;
        }

        if (rc == -1 && errno == ETIMEDOUT && wake_kind == WAKE_MANIFEST) {
            /* Periodic manifest burst (WS1 §2.3 point 3) — from this thread,
             * so the app pays nothing. Re-arm and loop. */
            pinsight_manifest_emit("periodic");
            manifest_timer_arm_from_config();
            continue;
        }

        int reason;
        if (rc == -1 && errno == ETIMEDOUT) {
            /* Window deadline reached. End the window via the SAME machinery the
             * count path uses: win the latch (if a count policy already fired
             * this window, the CAS no-ops and we just disarm), then synthesize
             * the wakeup the app thread would have posted and fall through to the
             * existing handler. INTROSPECT vs plain switch is chosen from the
             * action itself, so INTROSPECT-on-timeout works for free. */
            window_end_action_t *ma = window_timer_ma;
            window_timer_armed = 0; /* one-shot; re-armed on reload / cyclic reset */
            if (!ma)
                continue;
            /* If a count policy already ended this window, no-op (the real
             * arbiter is the domain-level mode_change_fired, not ma->fired —
             * see window_already_ended). The ma->fired CAS additionally guards
             * the same-config case. */
            int expected = 0;
            if (window_already_ended(ma) ||
                !__atomic_compare_exchange_n(&ma->fired, &expected, 1, 0,
                                             __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST))
                continue; /* window already ended this cycle */
            fprintf(stderr, "PInsight: window_timeout (%ds) reached — ending "
                            "TRACING window\n", ma->window_timeout_sec);
            pending_mode_action = ma;
            reason = ma->introspect ? PINSIGHT_WAKEUP_INTROSPECT
                                    : PINSIGHT_WAKEUP_MODE_CHANGE;
        } else {
            if (control_shutdown) break;
            /* Consume wakeup reason atomically */
            reason = __atomic_exchange_n(&pending_wakeup_reason, 0,
                                         __ATOMIC_SEQ_CST);
        }

        /* 1. Config reload (SIGUSR1 or inotify) */
        if (reason & PINSIGHT_WAKEUP_CONFIG_RELOAD) {
            fprintf(stderr, "PInsight: Control thread reloading config\n");
            /* Force re-parse even if mtime is unchanged (file may have been
             * overwritten in-place within the same second). */
            pinsight_invalidate_config_mtime();
            pinsight_load_trace_config(NULL);
            /* Re-arm the window timer: a reload may add, extend, or clear
             * window_timeout. Restarts the deadline from now. */
            window_timer_arm_from_default();
#ifdef PINSIGHT_ENERGY
            /* Apply [Energy] measure off<->armed transitions (variant is
             * latched per run). Safe on this thread: the AMD-SMI constraint
             * only forbids main-thread reads during DSO teardown. */
            pinsight_energy_apply_config();
#endif
            /* The load above auto-refreshed the effective-config dump cache;
             * flush the new config's content file (no-op if unchanged config
             * or PINSIGHT_MANIFEST_DIR unset). The `window` burst that
             * references the new hash is emitted below, after mode handling —
             * file-before-burst order (ws1 design §2.8). */
            pinsight_manifest_dump_config();
            /* Note: we intentionally do NOT reset mode_change_fired here.
             * Config reload updates configuration (modes, rates, events)
             * but does not touch runtime state (counters, trigger guards).
             * Auto-triggers re-arm naturally when the user increases
             * max_num_traces in the config (new threshold > trace_count). */
        }

        /* 2 & 3. Auto-trigger: Mode Change and/or Introspection */
        if ((reason & (PINSIGHT_WAKEUP_INTROSPECT | PINSIGHT_WAKEUP_MODE_CHANGE)) && pending_mode_action) {
            window_end_action_t *ma = pending_mode_action;
            pending_mode_action = NULL;

            if (reason & PINSIGHT_WAKEUP_INTROSPECT) {
                control_execute_introspect(ma);
            }

            /* Apply end_action modes */
            int cyclic_resume = 0; /* detect TRACING resume for counter reset */
            for (int d = 0; d < num_domain; d++) {
                pinsight_domain_mode_t new_mode = ma->mode[d];
                /* NONE = user didn't specify → keep current mode */
                if (new_mode == PINSIGHT_DOMAIN_NONE)
                    continue;
                if (!domain_default_trace_config[d].mode_change_fired &&
                    new_mode != domain_default_trace_config[d].mode) {
                    domain_default_trace_config[d].last_mode =
                        domain_default_trace_config[d].mode;
                    domain_default_trace_config[d].mode = new_mode;
                    domain_default_trace_config[d].mode_change_fired = 1;
                    fprintf(stderr, "PInsight: Auto-trigger: %s mode -> %s\n",
                            domain_info_table[d].name,
                            pinsight_mode_str(new_mode));
                }
                if (new_mode == PINSIGHT_DOMAIN_TRACING)
                    cyclic_resume = 1;
            }

            /* Phase 2 §6.9.6: apply per-domain device_activity targets
             * ("HIP:device_activity=off") — trace_mode untouched, so host
             * tracing stays on. The control_apply_all_modes call below runs
             * apply_collect_state, which toggles the pool accordingly. */
            for (int d = 0; d < num_domain; d++) {
                if (!ma->np_set[d])
                    continue;
                int npidx = pinsight_get_nodepolicy_index(d, "device_activity");
                if (npidx < 0)
                    continue;
                domain_default_trace_config[d].nodepolicy[npidx] =
                    ma->np_target[d];
                fprintf(stderr,
                        "PInsight: Auto-trigger: %s device_activity -> %s\n",
                        domain_info_table[d].name,
                        pinsight_nodepolicy_str(ma->np_target[d].policy));
            }

            /* Cyclic INTROSPECT: if any domain resumes to TRACING,
             * advance the generation counter and reset latches so the
             * next cycle can fire with a full tracing window.
             *
             * The generation increment causes ALL lexgions (across all
             * threads) to auto-reset their trace_counter on the next
             * invocation of lexgion_post_trace_update(), regardless of
             * which lexgion fired this cycle. This ensures evenly
             * spaced cycles even when lexgions have different call rates. */
            if (cyclic_resume) {
                for (int d = 0; d < num_domain; d++) {
                    domain_default_trace_config[d].mode_change_fired = 0;
                }
                __atomic_store_n(&ma->fired, 0, __ATOMIC_SEQ_CST);
                ma->generation++;  /* all lexgions will see new gen and reset */
                fprintf(stderr,
                    "PInsight: Cyclic INTROSPECT: cycle %u complete, "
                    "latches reset for next cycle\n", ma->generation);
                /* Re-arm the window timer for the new TRACING window so each
                 * cycle is bounded. This re-arm (replacing any stale deadline
                 * each cycle) is what makes the timeout's CAS-no-op safe. */
                window_timer_arm_from_default();
            }
        }

        /* 4. Apply domain-specific changes (enable/disable callbacks) */
        if (reason & (PINSIGHT_WAKEUP_CONFIG_RELOAD |
                      PINSIGHT_WAKEUP_MODE_CHANGE |
                      PINSIGHT_WAKEUP_INTROSPECT)) {
            control_apply_all_modes();

            /* 5. WS1 Step 3: manifest transition burst + timer re-evaluation.
             * Config and/or modes changed — the burst re-stamps the new epoch
             * (config_hash, window_gen, modes-derived facts); the dormancy
             * predicate's inputs (domain modes, energy policy, interval) may
             * all have flipped, so re-arm/disarm the periodic timer. The
             * burst is a lifecycle burst — unconditional, not dormancy-gated
             * (§2.4). */
            manifest_timer_arm_from_config();
            pinsight_manifest_emit("window");
        }
    }

#ifdef PINSIGHT_ENERGY
    /* Close any open energy span (disarm = final read + energy_exit) from this
     * (live) control thread just before it dies, NOT from the library
     * destructor: AMD-SMI corrupts the heap if its gpu_metrics path is read on
     * the main thread during DSO teardown after the constructor's read. Reading
     * from a separate live thread is safe. Runs after the exit_pinsight marker
     * (pinsight_control_thread_stop is called right after it), preserving the
     * app-end-precedes-final-snapshot order. */
    pinsight_energy_disarm();
    pinsight_energy_fini();
#endif

    return NULL;
}

/* ================================================================
 * Public API
 * ================================================================ */
void pinsight_control_thread_start(void) {
    sem_init(&control_sem, 0, 0);
    control_shutdown = 0;

    int ret = pthread_create(&control_thread, NULL, pinsight_control_loop, NULL);
    if (ret != 0) {
        fprintf(stderr,
                "PInsight WARNING: Failed to create control thread: %s\n",
                strerror(ret));
    }
}

void pinsight_control_thread_stop(void) {
    control_shutdown = 1;
    sem_post(&control_sem);  /* Wake the thread so it can exit */
    pthread_join(control_thread, NULL);
    sem_destroy(&control_sem);
}

void pinsight_control_thread_wakeup(int reason) {
    __atomic_or_fetch(&pending_wakeup_reason, reason, __ATOMIC_SEQ_CST);
    sem_post(&control_sem);  /* async-signal-safe */
}

/* ================================================================
 * Signal handler — trivial, only does sem_post
 * ================================================================ */
static void pinsight_sigusr1_handler(int sig) {
    (void)sig;
    /* Use atomic OR — signal handler may run in any app thread context. */
    __atomic_or_fetch(&pending_wakeup_reason, PINSIGHT_WAKEUP_CONFIG_RELOAD,
                      __ATOMIC_SEQ_CST);
    sem_post(&control_sem);
}

void pinsight_install_signal_handler(void) {
    struct sigaction sa;
    sa.sa_handler = pinsight_sigusr1_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    if (sigaction(SIGUSR1, &sa, NULL) != 0) {
        fprintf(stderr,
                "PInsight WARNING: Failed to install SIGUSR1 handler: %s\n",
                strerror(errno));
    }
}

/* ================================================================
 * Set pending mode action — called from pinsight_fire_window_end
 * ================================================================ */
void pinsight_control_set_pending_action(window_end_action_t *ma) {
    pending_mode_action = ma;
}
