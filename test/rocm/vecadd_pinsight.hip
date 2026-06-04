// Simple HIP vecadd for testing PInsight's ROCTracer trace control.
// Build: hipcc -o vecadd_pinsight vecadd_pinsight.hip
// Run:   LD_PRELOAD=/path/to/libpinsight.so ./vecadd_pinsight [num_iters]
#include <hip/hip_runtime.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HIP_CHECK(call)                                                \
  do {                                                                 \
    hipError_t err = (call);                                           \
    if (err != hipSuccess) {                                           \
      fprintf(stderr, "HIP error %s:%d: %s\n",                        \
              __FILE__, __LINE__, hipGetErrorString(err));             \
      exit(1);                                                         \
    }                                                                  \
  } while (0)

__global__ void VecAdd(const int *A, const int *B, int *C, int N) {
    int i = blockDim.x * blockIdx.x + threadIdx.x;
    if (i < N)
        C[i] = A[i] + B[i];
}

static void initVec(int *vec, int n) {
    for (int i = 0; i < n; i++)
        vec[i] = i;
}

int main(int argc, char *argv[]) {
    int N = 50000;
    int num_iters = 10;  /* multiple iterations for rate control testing */
    if (argc > 1) num_iters = atoi(argv[1]);

    HIP_CHECK(hipSetDevice(0));

    size_t size = N * sizeof(int);
    int *h_A = (int *)malloc(size);
    int *h_B = (int *)malloc(size);
    int *h_C = (int *)malloc(size);

    initVec(h_A, N);
    initVec(h_B, N);
    memset(h_C, 0, size);

    int *d_A, *d_B, *d_C;
    HIP_CHECK(hipMalloc((void **)&d_A, size));
    HIP_CHECK(hipMalloc((void **)&d_B, size));
    HIP_CHECK(hipMalloc((void **)&d_C, size));

    int threadsPerBlock = 256;
    int blocksPerGrid = (N + threadsPerBlock - 1) / threadsPerBlock;

    for (int iter = 0; iter < num_iters; iter++) {
        HIP_CHECK(hipMemcpy(d_A, h_A, size, hipMemcpyHostToDevice));
        HIP_CHECK(hipMemcpy(d_B, h_B, size, hipMemcpyHostToDevice));

        VecAdd<<<blocksPerGrid, threadsPerBlock>>>(d_A, d_B, d_C, N);

        HIP_CHECK(hipMemcpy(h_C, d_C, size, hipMemcpyDeviceToHost));
        HIP_CHECK(hipDeviceSynchronize());
    }

    /* Verify */
    int pass = 1;
    for (int i = 0; i < N; ++i) {
        if (h_C[i] != h_A[i] + h_B[i]) { pass = 0; break; }
    }
    printf("VecAdd: %s (%d iterations)\n", pass ? "PASSED" : "FAILED", num_iters);

    HIP_CHECK(hipFree(d_A));
    HIP_CHECK(hipFree(d_B));
    HIP_CHECK(hipFree(d_C));
    free(h_A); free(h_B); free(h_C);

    /* hipDeviceReset flushes pending ROCTracer activity records before
     * PInsight's destructor calls roctracer_flush_activity_expl().
     * This mirrors the cudaDeviceReset() pattern in the CUDA test. */
    HIP_CHECK(hipDeviceReset());
    return 0;
}
