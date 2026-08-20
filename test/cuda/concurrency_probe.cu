// Multi-stream concurrency probe — the synthetic case behind commit 2384cd4.
//
// Build: nvcc -arch=sm_90 -o concurrency_probe concurrency_probe.cu
// Usage: ./concurrency_probe [num_streams] [spin_ms] [iters]
//
// Launches N independent spin kernels on N separate streams, each doing the
// same fixed amount of work, then waits for all of them.  On a GPU with the
// capacity to run them together, wall time per iteration is ~the time of ONE
// kernel.  If something forces kernels to run one-at-a-time it is ~N times
// that -- which is exactly what CUPTI_ACTIVITY_KIND_KERNEL collection does
// (see cupti_activity.h: "all kernel executions are serialized on the GPU").
//
// The kernel spins on the clock rather than doing arithmetic so that its
// duration is set by time, not by how fast the device retires FLOPs -- this
// keeps the serialized/concurrent ratio close to a clean N regardless of GPU.
//
// Prints the per-iteration wall time so the probe is meaningful on its own,
// but the authoritative check is the trace: run it under PInsight and feed
// the trace to analysis/gpu_kernel_concurrency.py, which measures overlap
// directly from the activity records instead of inferring it from wall time.
#include <cuda_runtime.h>
#include <cstdio>
#include <cstdlib>
#include <vector>

#define CK(x) do { cudaError_t e = (x); if (e) {                          \
    fprintf(stderr, "CUDA error %d (%s) at %s:%d\n", e,                   \
            cudaGetErrorString(e), __FILE__, __LINE__);                   \
    exit(1); } } while (0)

__global__ void spin(long long cycles, int *sink) {
    long long t0 = clock64();
    while (clock64() - t0 < cycles) { }
    if (threadIdx.x == 1 << 30) *sink = 1;   // never true; defeats DCE
}

int main(int argc, char **argv) {
    int nstreams = argc > 1 ? atoi(argv[1]) : 4;
    int spin_ms  = argc > 2 ? atoi(argv[2]) : 20;
    int iters    = argc > 3 ? atoi(argv[3]) : 20;

    int dev = 0;
    CK(cudaGetDevice(&dev));
    int clk_khz = 0;
    CK(cudaDeviceGetAttribute(&clk_khz, cudaDevAttrClockRate, dev));
    if (clk_khz <= 0) clk_khz = 1000000;            /* 1 GHz fallback */
    long long cycles = (long long)spin_ms * clk_khz;

    std::vector<cudaStream_t> streams(nstreams);
    for (int i = 0; i < nstreams; ++i)
        CK(cudaStreamCreate(&streams[i]));
    int *sink = nullptr;
    CK(cudaMalloc(&sink, sizeof(int)));

    // one warm-up iteration, not timed (context/module load)
    for (int i = 0; i < nstreams; ++i)
        spin<<<1, 32, 0, streams[i]>>>(cycles, sink);
    CK(cudaDeviceSynchronize());

    cudaEvent_t beg, end;
    CK(cudaEventCreate(&beg)); CK(cudaEventCreate(&end));
    CK(cudaEventRecord(beg));
    for (int it = 0; it < iters; ++it) {
        for (int i = 0; i < nstreams; ++i)
            spin<<<1, 32, 0, streams[i]>>>(cycles, sink);
        CK(cudaDeviceSynchronize());
    }
    CK(cudaEventRecord(end));
    CK(cudaEventSynchronize(end));
    float ms = 0.f;
    CK(cudaEventElapsedTime(&ms, beg, end));

    printf("concurrency_probe: %d streams x %d ms spin, %d iters\n",
           nstreams, spin_ms, iters);
    printf("  per-iteration wall: %.2f ms   (1 kernel = %d ms => ratio %.2fx; "
           "1.0 = concurrent, %.1f = fully serialized)\n",
           ms / iters, spin_ms, (ms / iters) / spin_ms, (double)nstreams);

    for (int i = 0; i < nstreams; ++i) CK(cudaStreamDestroy(streams[i]));
    CK(cudaFree(sink));
    cudaDeviceReset();   /* force CUPTI to flush pending activity buffers */
    return 0;
}
