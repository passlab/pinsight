// Test __builtin_return_address depth inside CUPTI callbacks.
// Only tests safe depths (0-9) based on backtrace frame count.
#include <stdio.h>
#include <string.h>
#include <cuda_runtime.h>
#include <cupti.h>
#include <execinfo.h>

static CUpti_SubscriberHandle subscriber;

__global__ void VecAdd(const int *A, const int *B, int *C, int N) {
    int i = blockDim.x * blockIdx.x + threadIdx.x;
    if (i < N) C[i] = A[i] + B[i];
}

void CUPTIAPI test_callback(void *userdata, CUpti_CallbackDomain domain,
                            CUpti_CallbackId cbid, const CUpti_CallbackData *cbInfo) {
    if (domain != CUPTI_CB_DOMAIN_RUNTIME_API) return;
    if (cbInfo->callbackSite != CUPTI_API_ENTER) return;

    const char *name = (cbid == CUPTI_RUNTIME_TRACE_CBID_cudaLaunchKernel_v7000)
                       ? "cudaLaunchKernel" : "cudaMemcpy";
    printf("\n=== %s CUPTI callback ===\n", name);
    if (cbInfo->symbolName) printf("symbolName: %s\n", cbInfo->symbolName);
    if (cbInfo->functionName) printf("functionName: %s\n", cbInfo->functionName);

    // Print backtrace to find the safe depth limit
    void *bt[20];
    int n = backtrace(bt, 20);
    char **syms = backtrace_symbols(bt, n);
    printf("Full backtrace (%d frames):\n", n);
    for (int i = 0; i < n; i++)
        printf("  [%2d] %p %s\n", i, bt[i], syms[i]);
    free(syms);

    // Only test depths up to (n-2) to avoid segfault
    int max_depth = (n > 2) ? n - 2 : 0;
    if (max_depth > 9) max_depth = 9;  // hard cap
    printf("\n__builtin_return_address (max safe depth=%d):\n", max_depth);
    void *addr;
    #define PRINT_RA(D) if (D <= max_depth) { addr = __builtin_return_address(D); printf("  depth %2d: %p\n", D, addr); }
    PRINT_RA(0); PRINT_RA(1); PRINT_RA(2); PRINT_RA(3); PRINT_RA(4);
    PRINT_RA(5); PRINT_RA(6); PRINT_RA(7); PRINT_RA(8); PRINT_RA(9);
    #undef PRINT_RA
}

int main() {
    int N = 1000;
    size_t size = N * sizeof(int);
    int *h_A = (int*)malloc(size), *h_B = (int*)malloc(size), *h_C = (int*)malloc(size);
    int *d_A, *d_B, *d_C;
    for (int i = 0; i < N; i++) { h_A[i] = i; h_B[i] = i; }

    cuptiSubscribe(&subscriber, (CUpti_CallbackFunc)test_callback, NULL);
    cuptiEnableCallback(1, subscriber, CUPTI_CB_DOMAIN_RUNTIME_API,
                        CUPTI_RUNTIME_TRACE_CBID_cudaLaunchKernel_v7000);
    cuptiEnableCallback(1, subscriber, CUPTI_CB_DOMAIN_RUNTIME_API,
                        CUPTI_RUNTIME_TRACE_CBID_cudaMemcpy_v3020);

    cudaMalloc((void**)&d_A, size);
    cudaMalloc((void**)&d_B, size);
    cudaMalloc((void**)&d_C, size);

    printf("main() address: %p\n", (void*)main);

    // TEST 1: Kernel launch (line ~68)
    cudaMemcpy(d_A, h_A, size, cudaMemcpyHostToDevice);
    cudaMemcpy(d_B, h_B, size, cudaMemcpyHostToDevice);
    VecAdd<<<4, 256>>>(d_A, d_B, d_C, N);  // <-- kernel launch line
    cudaDeviceSynchronize();

    // TEST 2: memcpy DtoH (line ~73)
    cudaMemcpy(h_C, d_C, size, cudaMemcpyDeviceToHost);

    cuptiUnsubscribe(subscriber);
    printf("\nResult: h_C[0]=%d h_C[1]=%d\n", h_C[0], h_C[1]);
    cudaFree(d_A); cudaFree(d_B); cudaFree(d_C);
    free(h_A); free(h_B); free(h_C);
    return 0;
}
