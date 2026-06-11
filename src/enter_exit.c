//
// Created by Yonghong Yan on 3/3/20.
//

#include <pinsight.h>
#include "pinsight_control_thread.h"
#include <sys/types.h>
#include <unistd.h>

#ifdef PINSIGHT_ENERGY
#include "energy.h"
#endif

#define LTTNG_UST_TRACEPOINT_CREATE_PROBES
#define LTTNG_UST_TRACEPOINT_DEFINE
#include "enter_exit_lttng_ust_tracepoint.h"

/* Priority 200 (> LTTng-UST's provider-registration priority of 150) so the
 * enter/exit tracepoints emitted here fire after providers are registered and
 * before they are torn down.  A lower priority drops these events. */
void enter_pinsight_func() __attribute__((constructor(200)));
void exit_pinsight_func() __attribute__((destructor(200)));

#pragma startup enter_pinsight_func 100
#pragma exit exit_pinsight_func 100

unsigned int pid;
char hostname[48];

#ifdef PINSIGHT_CUDA
extern void LTTNG_CUPTI_Init(void);
extern void LTTNG_CUPTI_Fini(void);
#endif
#ifdef PINSIGHT_HIP
extern void LTTNG_ROCTRACER_Init(void);
extern void LTTNG_ROCTRACER_Fini(void);
#endif

/**
 * This has to be the initial/main thread of the main program and I do not see a
 * situation that this is not true
 */
void enter_pinsight_func() {
  pid = getpid();
  gethostname(hostname, 48);
  // printf("entering pinsight at host: %s by process: %d\n", hostname, pid);
#ifdef PINSIGHT_ENERGY
  /* Baseline energy snapshot precedes the enter_pinsight app-start marker. */
  pinsight_energy_init();
  pinsight_energy_snapshot_enter();
#endif
  lttng_ust_tracepoint(pinsight_enter_exit_lttng_ust, enter_pinsight);

  initial_setup_trace_config();

#ifdef PINSIGHT_CUDA
  LTTNG_CUPTI_Init();
#endif
#ifdef PINSIGHT_HIP
  LTTNG_ROCTRACER_Init();
#endif

  /* Start the control thread after all domain callbacks are registered so
   * the control thread never races with the initial enable calls. */
  pinsight_control_thread_start();
  pinsight_install_signal_handler();
#ifdef PINSIGHT_BACKTRACE
#endif
}

void exit_pinsight_func() {
  // printf("exiting pinsight at host: %s by process: %d\n", hostname, pid);
  lttng_ust_tracepoint(pinsight_enter_exit_lttng_ust, exit_pinsight);
#ifdef PINSIGHT_CUDA
  LTTNG_CUPTI_Fini();
#endif
#ifdef PINSIGHT_HIP
  LTTNG_ROCTRACER_Fini();
#endif
  /* Stop the control thread after domain finalization. The control thread emits
   * the closing energy_exit snapshot as it shuts down (inside this stop call) —
   * it must run on a live thread, not this destructor, because AMD-SMI is unsafe
   * to read on the main thread during DSO teardown. See pinsight_control_thread.c. */
  pinsight_control_thread_stop();
}
