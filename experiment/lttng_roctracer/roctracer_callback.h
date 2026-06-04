/*
 * roctracer_callback.h
 *
 * ROCTracer tracing Init/Fini API used by vecadd.hip.
 * When built without -DWITH_ROCTRACER these expand to no-ops.
 */
#ifndef ROCTRACER_CALLBACK_H
#define ROCTRACER_CALLBACK_H

#ifdef __cplusplus
extern "C" {
#endif

void ROCTRACER_Init(void);
void ROCTRACER_Fini(void);

#ifdef __cplusplus
}
#endif

#endif /* ROCTRACER_CALLBACK_H */
