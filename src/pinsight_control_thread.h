/**
 * pinsight_control_thread.h
 *
 * Dedicated PInsight control thread for centralized configuration management.
 * Implements a single-writer / multiple-reader (SWMR) pattern:
 *   - Control thread (writer): reads config, updates mode flags, enables/disables
 *     CUPTI callbacks, registers/deregisters OMPT callbacks, updates knobs.
 *   - App threads (readers): only read volatile config values — no atomics,
 *     no flag checks, no reconfig logic in the hot path.
 *
 * Wakeup sources:
 *   - SIGUSR1 signal → sem_post(&control_sem)
 *   - Auto-trigger (mode_after) → pinsight_control_thread_wakeup()
 *   - inotify config file change (Linux only, future)
 */
#ifndef PINSIGHT_CONTROL_THREAD_H
#define PINSIGHT_CONTROL_THREAD_H

#include <pthread.h>
#include <semaphore.h>
#include "pinsight_config.h"
#include "trace_config.h"

/**
 * Application pause flag for introspection support.
 * Written ONLY by the control thread.
 * Read by app threads at callback entry points.
 */
extern volatile int pinsight_app_paused;

/**
 * Start the control thread.
 * Called from enter_pinsight_func() (library constructor).
 */
void pinsight_control_thread_start(void);

/**
 * Stop the control thread and join.
 * Called from exit_pinsight_func() (library destructor).
 */
void pinsight_control_thread_stop(void);

/**
 * Wake the control thread to apply pending changes.
 * Called from:
 *   - Signal handler (SIGUSR1) — async-signal-safe via sem_post()
 *   - Auto-trigger when lexgion reaches max_num_traces
 *   - inotify watcher (future)
 *
 * @param reason  Bitmask of PINSIGHT_WAKEUP_* flags indicating what to do.
 */
#define PINSIGHT_WAKEUP_CONFIG_RELOAD  0x01  /* Re-read config file */
#define PINSIGHT_WAKEUP_MODE_CHANGE    0x02  /* Apply mode_after changes */
#define PINSIGHT_WAKEUP_INTROSPECT     0x04  /* Run introspection + pause */

void pinsight_control_thread_wakeup(int reason);

/**
 * Install the SIGUSR1 signal handler (trivial sem_post).
 * Called from enter_pinsight_func() after starting the control thread.
 */
void pinsight_install_signal_handler(void);

/**
 * Check if app is paused and block until resumed.
 * Called at the entry of each domain callback (CUPTI, OMPT, PMPI).
 * Fast path: single volatile read (~1ns when not paused).
 */
static inline void pinsight_check_pause(void) {
    if (__builtin_expect(pinsight_app_paused, 0)) {
        /* Declared in pinsight_control_thread.c */
        extern pthread_mutex_t pinsight_pause_mutex;
        extern pthread_cond_t  pinsight_pause_cond;

        pthread_mutex_lock(&pinsight_pause_mutex);
        while (pinsight_app_paused) {
            pthread_cond_wait(&pinsight_pause_cond, &pinsight_pause_mutex);
        }
        pthread_mutex_unlock(&pinsight_pause_mutex);
    }
}

/**
 * Set the pending introspection action for the control thread.
 * Called from pinsight_fire_mode_triggers() when mode_after.introspect == 1.
 */
void pinsight_control_set_introspect(trace_mode_after_t *ma);

/* ================================================================
 * Domain-specific apply functions — called by control thread only.
 * Implemented in each domain's source file and linked via extern.
 * ================================================================ */

#ifdef PINSIGHT_CUDA
/**
 * Enable/disable all CUPTI callbacks based on current CUDA domain mode.
 * Calls cuptiEnableCallback() — process-global, thread-safe.
 */
extern void pinsight_control_cuda_apply_mode(void);
#endif

#ifdef PINSIGHT_OMPT_CALLBACKS
/**
 * Register/deregister OMPT callbacks based on current OpenMP domain mode.
 * Calls ompt_set_callback() — tested safe from non-OpenMP threads on libomp.
 */
extern void pinsight_control_openmp_apply_mode(void);
#endif

/* MPI: no apply function needed — PMPI wrappers read volatile mode flag directly */

#endif /* PINSIGHT_CONTROL_THREAD_H */
