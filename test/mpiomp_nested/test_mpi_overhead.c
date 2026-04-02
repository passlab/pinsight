/**
 * test_mpi_overhead.c — PInsight MPI overhead benchmark
 *
 * Repeats a hot halo-exchange loop (Isend/Irecv/Waitall) + Allreduce
 * NITER times so per-call overhead is measurable.
 *
 * Usage:  mpirun -np 48 ./test_mpi_overhead [NITER]
 *   Default NITER = 10000
 *
 * Reports:
 *   - Total wall time (from MPI_Wtime, rank 0 view)
 *   - Per-iteration time in microseconds
 */
#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MSGLEN  64   /* ints per halo message */

int main(int argc, char *argv[])
{
    MPI_Init(&argc, &argv);

    int rank, size;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    int niter = (argc > 1) ? atoi(argv[1]) : 10000;

    /* ring neighbours */
    int next = (rank + 1) % size;
    int prev = (rank - 1 + size) % size;

    int *sbuf = (int *)malloc(MSGLEN * sizeof(int));
    int *rbuf = (int *)malloc(MSGLEN * sizeof(int));
    for (int i = 0; i < MSGLEN; i++) sbuf[i] = rank * MSGLEN + i;

    MPI_Request reqs[2];

    /* warm-up: 100 iters, not timed */
    for (int i = 0; i < 100; i++) {
        MPI_Irecv(rbuf, MSGLEN, MPI_INT, prev, 0, MPI_COMM_WORLD, &reqs[0]);
        MPI_Isend(sbuf, MSGLEN, MPI_INT, next, 0, MPI_COMM_WORLD, &reqs[1]);
        MPI_Waitall(2, reqs, MPI_STATUSES_IGNORE);
        int lsum = rank, gsum = 0;
        MPI_Allreduce(&lsum, &gsum, 1, MPI_INT, MPI_SUM, MPI_COMM_WORLD);
    }
    MPI_Barrier(MPI_COMM_WORLD);

    /* --- timed region --- */
    double t0 = MPI_Wtime();

    for (int i = 0; i < niter; i++) {
        /* non-blocking halo exchange (AMReX pattern) */
        MPI_Irecv(rbuf, MSGLEN, MPI_INT, prev, 0, MPI_COMM_WORLD, &reqs[0]);
        MPI_Isend(sbuf, MSGLEN, MPI_INT, next, 0, MPI_COMM_WORLD, &reqs[1]);
        MPI_Waitall(2, reqs, MPI_STATUSES_IGNORE);

        /* global reduction (time-step sync in AMR codes) */
        int lsum = rank, gsum = 0;
        MPI_Allreduce(&lsum, &gsum, 1, MPI_INT, MPI_SUM, MPI_COMM_WORLD);
    }

    MPI_Barrier(MPI_COMM_WORLD);
    double elapsed = MPI_Wtime() - t0;

    /* reduce to max across ranks (worst-case wall time) */
    double max_elapsed;
    MPI_Reduce(&elapsed, &max_elapsed, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);

    if (rank == 0) {
        printf("Ranks=%d  Iters=%d  Total=%.4f s  Per-iter=%.2f us  "
               "Per-call=%.2f us\n",
               size, niter, max_elapsed,
               max_elapsed / niter * 1e6,
               max_elapsed / niter / 4 * 1e6);  /* 4 MPI calls per iter */
    }

    free(sbuf);
    free(rbuf);
    MPI_Finalize();
    return 0;
}
