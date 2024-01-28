#include <stdio.h>
#include <cuda.h>

// Vector addition kernel
__global__ void VecAdd(const int *A, const int *B, int *C, int N) {
    int i = blockDim.x * blockIdx.x + threadIdx.x;
    if (i < N)
        C[i] = A[i] + B[i];
}

// Initialize a vector
static void initVec(int *vec, int n) {
    for (int i = 0; i < n; i++)
        vec[i] = i;
}

int main(int argc, char *argv[]) {
    CUcontext context = 0;
    CUdevice device = 0;
    int N = 50000000;
    size_t size = N * sizeof(int);
    int threadsPerBlock = 0;
    int blocksPerGrid = 0;
    int sum, i;
    int *h_A, *h_B, *h_C;
    int *d_A, *d_B, *d_C;

    cuInit(0);
    cuCtxCreate(&context, 0, device);

    // Allocate input vectors h_A and h_B in host memory
    h_A = (int *) malloc(size);
    h_B = (int *) malloc(size);
    h_C = (int *) malloc(size);

    // Initialize input vectors
    initVec(h_A, N);
    initVec(h_B, N);
    memset(h_C, 0, size);

    // Allocate vectors in device memory
    cudaMalloc((void **) &d_A, size);
    cudaMalloc((void **) &d_B, size);
    cudaMalloc((void **) &d_C, size);

    // Copy vectors from host memory to device memory
    cudaMemcpy(d_A, h_A, size, cudaMemcpyHostToDevice);
    cudaMemcpy(d_B, h_B, size, cudaMemcpyHostToDevice);

    // Invoke kernel
    threadsPerBlock = 256;
    blocksPerGrid = (N + threadsPerBlock - 1) / threadsPerBlock;

    VecAdd << < blocksPerGrid, threadsPerBlock >> > (d_A, d_B, d_C, N);
    cudaDeviceSynchronize();

    // Copy result from device memory to host memory
    // h_C contains the result in host memory
    cudaMemcpy(h_C, d_C, size, cudaMemcpyDeviceToHost);
    cudaDeviceSynchronize();

    // Verify result
    for (i = 0; i < N; ++i) {
        sum = h_A[i] + h_B[i];
        if (h_C[i] != sum) {
            printf("kernel execution FAILED: %d vs %d\n", h_C[i], sum);
            break;
        }
    }

    cudaFree(d_A);
    cudaFree(d_B);
    cudaFree(d_C);

    free(h_A);
    free(h_B);
    free(h_C);

    cudaDeviceSynchronize();
    return 0;
}
