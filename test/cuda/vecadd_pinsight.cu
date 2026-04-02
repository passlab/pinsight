// Simple CUDA vecadd for testing PInsight's CUPTI trace control.
// Build: nvcc -o vecadd_pinsight vecadd_pinsight.cu -lcuda
// Run:   LD_PRELOAD=/path/to/libpinsight.so ./vecadd_pinsight
#include <stdio.h>
#include <string.h>
#include <cuda_runtime.h>

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
    int num_iters = 10;  // multiple iterations for rate control testing
    if (argc > 1) num_iters = atoi(argv[1]);

    size_t size = N * sizeof(int);
    int *h_A = (int *)malloc(size);
    int *h_B = (int *)malloc(size);
    int *h_C = (int *)malloc(size);

    initVec(h_A, N);
    initVec(h_B, N);
    memset(h_C, 0, size);

    int *d_A, *d_B, *d_C;
    cudaMalloc((void **)&d_A, size);
    cudaMalloc((void **)&d_B, size);
    cudaMalloc((void **)&d_C, size);

    int threadsPerBlock = 256;
    int blocksPerGrid = (N + threadsPerBlock - 1) / threadsPerBlock;

    for (int iter = 0; iter < num_iters; iter++) {
        cudaMemcpy(d_A, h_A, size, cudaMemcpyHostToDevice);
        cudaMemcpy(d_B, h_B, size, cudaMemcpyHostToDevice);

        VecAdd<<<blocksPerGrid, threadsPerBlock>>>(d_A, d_B, d_C, N);

        cudaMemcpy(h_C, d_C, size, cudaMemcpyDeviceToHost);
        cudaDeviceSynchronize();
    }

    // Verify
    int pass = 1;
    for (int i = 0; i < N; ++i) {
        if (h_C[i] != h_A[i] + h_B[i]) { pass = 0; break; }
    }
    printf("VecAdd: %s (%d iterations)\n", pass ? "PASSED" : "FAILED", num_iters);

    cudaFree(d_A); cudaFree(d_B); cudaFree(d_C);
    free(h_A); free(h_B); free(h_C);

    /* cudaDeviceReset destroys the CUDA context, which causes CUPTI to
     * flush all pending activity buffers (triggering bufferCompleted).
     * This must happen before pinsight's destructor calls cuptiActivityFlushAll,
     * otherwise the CUDA runtime may already be gone. */
    cudaDeviceReset();
    return 0;
}
