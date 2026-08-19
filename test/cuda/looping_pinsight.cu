// Long-running CUDA loop for PInsight runtime-mode and activity-collection testing.
//
// Build: nvcc -arch=sm_75 -o looping_pinsight looping_pinsight.cu   (or: make looping_pinsight)
//
// CUDA counterpart of test/rocm/looping_pinsight.hip.  Each iteration launches one
// kernel + cudaDeviceSynchronize, then sleeps, so an external driver can flip the
// CUDA trace mode (TRACING <-> MONITORING <-> STANDBY) between iterations via
// SIGUSR1 config reload and observe the effect.  Unlike the short vecadd test this
// runs for several seconds, which is what makes it useful for exercising:
//   - rate-controlled tracing over many region executions,
//   - the 4-mode transitions (OFF/STANDBY/MONITORING/TRACING) at runtime,
//   - the CUPTI activity enable -> disable -> RE-ENABLE cycle (the path that only
//     fires when the CUDA domain leaves and re-enters TRACING).  Note the CUPTI
//     asymmetry vs ROCTracer: "off" is FlushAll + cuptiActivityDisable(all kinds),
//     because CUPTI has no way to deregister the buffer callbacks.
//
// Usage:  ./looping_pinsight [num_iters] [sleep_ms]   (default 60 iters, 100 ms)
// See cyclic_mode_test.sh for a driver that cycles the mode while this runs, and
// device_activity_test.sh for the node-policy gate test.
#include <cuda_runtime.h>
#include <cstdio>
#include <cstdlib>
#include <unistd.h>

#define CK(x) do { cudaError_t e = (x); if (e) {                          \
    fprintf(stderr, "CUDA error %d (%s) at %s:%d\n", e,                   \
            cudaGetErrorString(e), __FILE__, __LINE__);                   \
    exit(1); } } while (0)

__global__ void bump(int *a, int n) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i < n) a[i] += 1;
}

int main(int argc, char **argv) {
    int iters    = argc > 1 ? atoi(argv[1]) : 60;
    int sleep_ms = argc > 2 ? atoi(argv[2]) : 100;
    int N = 1 << 16;

    int *d = nullptr;
    CK(cudaMalloc(&d, N * sizeof(int)));
    CK(cudaMemset(d, 0, N * sizeof(int)));

    int threadsPerBlock = 256;
    int blocksPerGrid = (N + threadsPerBlock - 1) / threadsPerBlock;

    for (int it = 0; it < iters; ++it) {
        bump<<<blocksPerGrid, threadsPerBlock>>>(d, N);
        CK(cudaDeviceSynchronize());
        fprintf(stderr, "ITER %d\n", it);
        fflush(stderr);               /* survive even when stderr is file-buffered */
        usleep(sleep_ms * 1000);
    }

    CK(cudaFree(d));
    printf("looping_pinsight: done %d iters (%d ms each)\n", iters, sleep_ms);

    /* cudaDeviceReset destroys the CUDA context, which makes CUPTI flush all
     * pending activity buffers before PInsight's destructor runs. */
    cudaDeviceReset();
    return 0;
}
