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
#include "pinsight.h"
#include "pinsight_config.h"
#include "trace_config.h"
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
static trace_mode_after_t *pending_mode_action = NULL;

/* ================================================================
 * Introspection support — moved from pinsight.c
 *
 * Implements the pause semantics:
 *   timeout > 0: pause for N seconds (interruptible by SIGUSR1)
 *   timeout = 0: no pause, just run script
 *   timeout = -1: pause indefinitely until SIGUSR1
 * ================================================================ */
static void control_execute_introspect(trace_mode_after_t *ma) {
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
    if (ma->introspect_timeout == 0) {
        /* No pause — just ran the script, continue */
        return;
    }

    /* Pause the application */
    pthread_mutex_lock(&pinsight_pause_mutex);
    pinsight_app_paused = 1;
    pthread_mutex_unlock(&pinsight_pause_mutex);
    fprintf(stderr, "PInsight: Application paused for introspection\n");

    if (ma->introspect_timeout > 0) {
        /* Timed wait — interruptible by SIGUSR1 → sem_post */
        struct timespec deadline;
        clock_gettime(CLOCK_REALTIME, &deadline);
        deadline.tv_sec += ma->introspect_timeout;

        int ret = sem_timedwait(&control_sem, &deadline);
        if (ret == -1 && errno == ETIMEDOUT) {
            fprintf(stderr, "PInsight: INTROSPECT timeout (%ds), auto-resuming\n",
                    ma->introspect_timeout);
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

    while (!control_shutdown) {
        /* Block until signaled — zero CPU when idle.
         * Restart on EINTR (which shouldn't happen since SIGUSR1 is
         * blocked, but defensive coding). */
        while (sem_wait(&control_sem) == -1 && errno == EINTR)
            continue;

        if (control_shutdown) break;

        /* Consume wakeup reason atomically */
        int reason = __atomic_exchange_n(&pending_wakeup_reason, 0,
                                          __ATOMIC_SEQ_CST);

        /* 1. Config reload (SIGUSR1 or inotify) */
        if (reason & PINSIGHT_WAKEUP_CONFIG_RELOAD) {
            fprintf(stderr, "PInsight: Control thread reloading config\n");
            pinsight_load_trace_config(NULL);
            /* Note: we intentionally do NOT reset mode_change_fired here.
             * Config reload updates configuration (modes, rates, events)
             * but does not touch runtime state (counters, trigger guards).
             * Auto-triggers re-arm naturally when the user increases
             * max_num_traces in the config (new threshold > trace_count). */
        }

        /* 2 & 3. Auto-trigger: Mode Change and/or Introspection */
        if ((reason & (PINSIGHT_WAKEUP_INTROSPECT | PINSIGHT_WAKEUP_MODE_CHANGE)) && pending_mode_action) {
            trace_mode_after_t *ma = pending_mode_action;
            pending_mode_action = NULL;

            if (reason & PINSIGHT_WAKEUP_INTROSPECT) {
                control_execute_introspect(ma);
            }

            /* Apply mode_after modes */
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
            }
        }

        /* 4. Apply domain-specific changes (enable/disable callbacks) */
        if (reason & (PINSIGHT_WAKEUP_CONFIG_RELOAD |
                      PINSIGHT_WAKEUP_MODE_CHANGE |
                      PINSIGHT_WAKEUP_INTROSPECT)) {
            control_apply_all_modes();
        }
    }

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
 * Set pending mode action — called from pinsight_fire_mode_triggers
 * ================================================================ */
void pinsight_control_set_pending_action(trace_mode_after_t *ma) {
    pending_mode_action = ma;
}
