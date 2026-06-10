/*
 * vecadd.hip — HIP vector addition: C[i] = A[i] + B[i]
 *
 * Calls ROCTRACER_Init/Fini from roctracer_callback.h.
 * Three build phases controlled by Makefile flags:
 *   Phase 1 (default)       : pure HIP, no tracing
 *   Phase 2 (-DWITH_ROCTRACER): ROCTracer callbacks + printf
 *   Phase 3 (+DWITH_LTTNG)  : ROCTracer callbacks + LTTng UST
 */
#include <hip/hip_runtime.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "roctracer_callback.h"

/* ------------------------------------------------------------------ */
/* HIP error check helper                                              */
/* ------------------------------------------------------------------ */
#define HIP_CHECK(call)                                                \
  do {                                                                 \
    hipError_t err = (call);                                           \
    if (err != hipSuccess) {                                           \
      fprintf(stderr, "HIP error %s:%d: %s\n",                        \
              __FILE__, __LINE__, hipGetErrorString(err));             \
      exit(1);                                                         \
    }                                                                  \
  } while (0)

/* ------------------------------------------------------------------ */
/* GPU kernel                                                          */
/* ------------------------------------------------------------------ */
__global__ void vecAdd(const float *A, const float *B, float *C, int N) {
    int i = blockDim.x * blockIdx.x + threadIdx.x;
    if (i < N)
        C[i] = A[i] + B[i];
}

/* ------------------------------------------------------------------ */
/* main                                                                */
/* ------------------------------------------------------------------ */
int main(void) {
    const int N    = 1 << 20;          /* 1M elements = 4 MB per array */
    const size_t sz = N * sizeof(float);

    /* Create a HIP context before Init (needed for activity pool) */
    ///HIP_CHECK(hipSetDevice(0));

    /* ----- Phase 2/3: start ROCTracer interception ----- */
    ROCTRACER_Init();

    /* Allocate and initialize host arrays */
    float *h_A = (float *)malloc(sz);
    float *h_B = (float *)malloc(sz);
    float *h_C = (float *)malloc(sz);
    for (int i = 0; i < N; i++) { h_A[i] = (float)i; h_B[i] = (float)(2 * i); }
    memset(h_C, 0, sz);

    /* Allocate device arrays */
    float *d_A, *d_B, *d_C;
    HIP_CHECK(hipMalloc(&d_A, sz));
    HIP_CHECK(hipMalloc(&d_B, sz));
    HIP_CHECK(hipMalloc(&d_C, sz));

    /* Host → Device copies */
    HIP_CHECK(hipMemcpy(d_A, h_A, sz, hipMemcpyHostToDevice));
    HIP_CHECK(hipMemcpy(d_B, h_B, sz, hipMemcpyHostToDevice));

    /* Launch kernel */
    int threadsPerBlock = 256;
    int blocksPerGrid   = (N + threadsPerBlock - 1) / threadsPerBlock;
    vecAdd<<<blocksPerGrid, threadsPerBlock>>>(d_A, d_B, d_C, N);
    HIP_CHECK(hipDeviceSynchronize());

    /* Device → Host copy */
    HIP_CHECK(hipMemcpy(h_C, d_C, sz, hipMemcpyDeviceToHost));

    /* ----- Phase 2/3: stop ROCTracer interception ----- */
    ROCTRACER_Fini();

    /* Verify */
    int errors = 0;
    for (int i = 0; i < N; i++) {
        float expected = h_A[i] + h_B[i];
        if (h_C[i] != expected) { errors++; }
    }
    printf("vecAdd %s (%d errors out of %d)\n",
           errors ? "FAILED" : "PASSED", errors, N);

    HIP_CHECK(hipFree(d_A));
    HIP_CHECK(hipFree(d_B));
    HIP_CHECK(hipFree(d_C));
    free(h_A); free(h_B); free(h_C);
    return errors ? 1 : 0;
}
