/**
 * test_mpi_full.c — PInsight MPI wrapper coverage test
 *
 * Exercises all MPI functions now implemented in pmpi_mpi.c:
 *   MPI_Init/Finalize, Send/Recv, Sendrecv,
 *   Isend/Irecv/Waitall (async halo exchange pattern),
 *   Bcast, Barrier, Reduce, Allreduce,
 *   Scatter, Gather, Allgather
 *
 * Run with 4 ranks:  mpirun -np 4 ./test_mpi_full
 */
#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define N 16   /* elements per rank */

int main(int argc, char *argv[])
{
    MPI_Init(&argc, &argv);

    int rank, size;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    /* ---- MPI_Bcast ---- */
    int param = (rank == 0) ? 42 : 0;
    MPI_Bcast(&param, 1, MPI_INT, 0, MPI_COMM_WORLD);

    /* ---- MPI_Barrier ---- */
    MPI_Barrier(MPI_COMM_WORLD);

    /* ---- MPI_Send / MPI_Recv (ring) ---- */
    int send_val = rank;
    int recv_val = -1;
    int next = (rank + 1) % size;
    int prev = (rank - 1 + size) % size;
    if (rank % 2 == 0) {
        MPI_Send(&send_val, 1, MPI_INT, next, 0, MPI_COMM_WORLD);
        MPI_Recv(&recv_val, 1, MPI_INT, prev, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
    } else {
        MPI_Recv(&recv_val, 1, MPI_INT, prev, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        MPI_Send(&send_val, 1, MPI_INT, next, 0, MPI_COMM_WORLD);
    }

    /* ---- MPI_Sendrecv (bidirectional halo) ---- */
    int sr_send = rank, sr_recv = -1;
    MPI_Sendrecv(&sr_send, 1, MPI_INT, next, 1,
                 &sr_recv, 1, MPI_INT, prev, 1,
                 MPI_COMM_WORLD, MPI_STATUS_IGNORE);

    /* ---- MPI_Isend / MPI_Irecv / MPI_Waitall (async halo — AMReX pattern) ---- */
    int isend_buf = rank, irecv_buf = -1;
    MPI_Request reqs[2];
    MPI_Irecv(&irecv_buf, 1, MPI_INT, prev, 2, MPI_COMM_WORLD, &reqs[0]);
    MPI_Isend(&isend_buf, 1, MPI_INT, next, 2, MPI_COMM_WORLD, &reqs[1]);
    MPI_Waitall(2, reqs, MPI_STATUSES_IGNORE);

    /* ---- MPI_Reduce ---- */
    int local_sum = rank, global_sum = 0;
    MPI_Reduce(&local_sum, &global_sum, 1, MPI_INT, MPI_SUM, 0, MPI_COMM_WORLD);

    /* ---- MPI_Allreduce ---- */
    int all_sum = 0;
    MPI_Allreduce(&local_sum, &all_sum, 1, MPI_INT, MPI_SUM, MPI_COMM_WORLD);

    /* ---- MPI_Scatter / MPI_Gather ---- */
    int *scatter_buf = NULL;
    if (rank == 0) {
        scatter_buf = (int *)malloc(size * sizeof(int));
        for (int i = 0; i < size; i++) scatter_buf[i] = i * 10;
    }
    int my_elem = 0;
    MPI_Scatter(scatter_buf, 1, MPI_INT, &my_elem, 1, MPI_INT, 0, MPI_COMM_WORLD);

    int *gather_buf = NULL;
    if (rank == 0) gather_buf = (int *)malloc(size * sizeof(int));
    MPI_Gather(&my_elem, 1, MPI_INT, gather_buf, 1, MPI_INT, 0, MPI_COMM_WORLD);

    /* ---- MPI_Allgather ---- */
    int *all_elems = (int *)malloc(size * sizeof(int));
    MPI_Allgather(&my_elem, 1, MPI_INT, all_elems, 1, MPI_INT, MPI_COMM_WORLD);

    /* correctness check on rank 0 */
    if (rank == 0) {
        int expected = size * (size - 1) / 2;
        printf("[rank 0] Bcast param=%d (expect 42)\n", param);
        printf("[rank 0] Allreduce sum=%d (expect %d) %s\n",
               all_sum, expected, all_sum == expected ? "PASS" : "FAIL");
        printf("[rank 0] Allgather[0]=%d [%d]=%d (expect 0 and %d) %s\n",
               all_elems[0], size-1, all_elems[size-1], (size-1)*10,
               (all_elems[0]==0 && all_elems[size-1]==(size-1)*10) ? "PASS" : "FAIL");
        free(scatter_buf);
        free(gather_buf);
    }
    free(all_elems);

    MPI_Barrier(MPI_COMM_WORLD);
    MPI_Finalize();
    return 0;
}
