/**
 * ompt_callback.h — Declarations of PInsight's OMPT callback functions.
 *
 * These callbacks are implemented in ompt_callback.c.  When that file is
 * linked into the final binary it defines PINSIGHT_OMPT_CALLBACKS before
 * including this header, which exposes the real extern declarations.
 *
 * Translation units that do NOT link ompt_callback.o (e.g. test_config_parser)
 * include this header without PINSIGHT_OMPT_CALLBACKS defined, so every
 * callback name expands to NULL — safe for storing in event_table.
 */
#ifndef OMPT_CALLBACK_H
#define OMPT_CALLBACK_H

#include "trace_config.h"
#include <omp-tools.h>

/* OpenMP domain globals — defined in ompt_callback.c */
extern int OpenMP_domain_index;
extern domain_info_t *OpenMP_domain_info;
extern domain_trace_config_t *OpenMP_trace_config;

/* Re-register or deregister OpenMP callbacks based on current domain mode.
 * Call after config reload (SIGUSR1) to sync OMPT callbacks with new mode. */
extern void pinsight_register_openmp_callbacks(void);

/* Re-register parallel_begin/end when waking from OFF mode via SIGUSR1.
 * Called from the signal handler — only safe because OFF mode has no
 * callbacks in flight.  Named with _openmp suffix for future domain
 * extensibility (MPI/CUDA will need their own wakeup mechanisms). */
extern void pinsight_wakeup_from_off_openmp(void);

#ifdef PINSIGHT_OMPT_CALLBACKS

/* Real declarations — resolved by the linker against ompt_callback.o */
extern void on_ompt_callback_thread_begin(ompt_thread_t, ompt_data_t *);
extern void on_ompt_callback_thread_end(ompt_data_t *);
extern void on_ompt_callback_parallel_begin(ompt_data_t *, const ompt_frame_t *,
                                            ompt_data_t *, unsigned int, int,
                                            const void *);
extern void on_ompt_callback_parallel_end(ompt_data_t *, ompt_data_t *, int,
                                          const void *);
extern void on_ompt_callback_implicit_task(ompt_scope_endpoint_t, ompt_data_t *,
                                           ompt_data_t *, unsigned int,
                                           unsigned int, int);
extern void on_ompt_callback_work(ompt_work_t, ompt_scope_endpoint_t,
                                  ompt_data_t *, ompt_data_t *, uint64_t,
                                  const void *);
extern void on_ompt_callback_masked(ompt_scope_endpoint_t, ompt_data_t *,
                                    ompt_data_t *, const void *);
extern void on_ompt_callback_sync_region(ompt_sync_region_t,
                                         ompt_scope_endpoint_t, ompt_data_t *,
                                         ompt_data_t *, const void *);
extern void on_ompt_callback_sync_region_wait(ompt_sync_region_t,
                                              ompt_scope_endpoint_t,
                                              ompt_data_t *, ompt_data_t *,
                                              const void *);

#else /* !PINSIGHT_OMPT_CALLBACKS */

/* Stubs: callbacks not linked — all names expand to NULL */
#define on_ompt_callback_thread_begin NULL
#define on_ompt_callback_thread_end NULL
#define on_ompt_callback_parallel_begin NULL
#define on_ompt_callback_parallel_end NULL
#define on_ompt_callback_implicit_task NULL
#define on_ompt_callback_work NULL
#define on_ompt_callback_masked NULL
#define on_ompt_callback_sync_region NULL
#define on_ompt_callback_sync_region_wait NULL

#endif /* PINSIGHT_OMPT_CALLBACKS */

#endif /* OMPT_CALLBACK_H */
