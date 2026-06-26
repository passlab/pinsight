// Long-running HIP loop for PInsight runtime-mode and activity-pool testing.
//
// Build: hipcc --offload-arch=gfx942 -o looping_pinsight looping_pinsight.hip \
//              -L/opt/rocm-7.2.1/lib -lamdhip64   (or: make looping_pinsight)
//
// Each iteration launches one kernel + hipDeviceSynchronize, then sleeps, so an
// external driver can flip the HIP trace mode (TRACING <-> MONITORING <-> STANDBY)
// between iterations via SIGUSR1 config reload and observe the effect.  Unlike the
// short vecadd test, this runs for several seconds, which is what makes it useful
// for exercising:
//   - rate-controlled tracing over many region executions,
//   - the 4-mode transitions (OFF/STANDBY/MONITORING/TRACING) at runtime,
//   - the ROCTracer activity pool enable -> disable -> RE-ENABLE cycle (the path
//     that only fires when the HIP domain leaves and re-enters TRACING).
//
// Usage:  ./looping_pinsight [num_iters] [sleep_ms]   (default 60 iters, 100 ms)
// See cyclic_mode_test.sh for a driver that cycles the mode while this runs.
#include <hip/hip_runtime.h>
#include <cstdio>
#include <cstdlib>
#include <unistd.h>

#define CK(x) do { hipError_t e = (x); if (e) {                      \
    fprintf(stderr, "HIP error %d at %s:%d\n", e, __FILE__, __LINE__); \
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
    CK(hipMalloc(&d, N * sizeof(int)));
    CK(hipMemset(d, 0, N * sizeof(int)));

    int threadsPerBlock = 256;
    int blocksPerGrid = (N + threadsPerBlock - 1) / threadsPerBlock;

    for (int it = 0; it < iters; ++it) {
        bump<<<blocksPerGrid, threadsPerBlock>>>(d, N);
        CK(hipDeviceSynchronize());
        fprintf(stderr, "ITER %d\n", it);
        fflush(stderr);               /* survive even when stderr is file-buffered */
        usleep(sleep_ms * 1000);
    }

    CK(hipFree(d));
    printf("looping_pinsight: done %d iters (%d ms each)\n", iters, sleep_ms);
    return 0;
}
