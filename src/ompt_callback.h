/**
 * ompt_callback.h — Declarations of PInsight's OMPT callback functions.
 *
 * These callbacks are implemented in ompt_callback.c.  This header is
 * included only from trace_domain_OpenMP.h, which is already guarded
 * by #ifdef PINSIGHT_OPENMP.
 *
 * Test binaries that do NOT link ompt_callback.o must provide their
 * own stub implementations for these symbols.
 */
#ifndef OMPT_CALLBACK_H
#define OMPT_CALLBACK_H

#include <omp-tools.h>

/* OMPT callback declarations — resolved by linker against ompt_callback.o */

/* Re-register or deregister OpenMP callbacks based on current domain mode.
 * Call after config reload to sync OMPT callbacks with new mode. */
extern void pinsight_register_openmp_callbacks(void);

/* Called by the control thread to apply OpenMP domain mode changes.
 * Tested safe from non-OpenMP pthread on LLVM libomp. */
extern void pinsight_control_openmp_apply_mode(void);

/* OMPT callback declarations — resolved by linker against ompt_callback.o */
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

#endif /* OMPT_CALLBACK_H */
