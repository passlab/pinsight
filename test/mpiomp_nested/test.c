#include <mpi.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <omp.h>

#define MSG_LEN 128
#define MSG_TAG 99

int main(int argc, char *argv[])
{
    int world_rank = 0, world_size = 0, name_len = 0, i = 0;
    char processor_name[MPI_MAX_PROCESSOR_NAME] = { 0x0 };
    char msg[MSG_LEN] = { 0x0 };
    MPI_Status status;

    /* MPI_Init(NULL, NULL); */
    MPI_Init(&argc, &argv); 
    MPI_Comm_rank(MPI_COMM_WORLD, &world_rank); 
    MPI_Comm_size(MPI_COMM_WORLD, &world_size);
    MPI_Get_processor_name(processor_name, &name_len);

    if (world_rank != 0) {
        sprintf(msg, "MPI: Greetings from %s, rank %d out of %d", processor_name, world_rank, world_size);
	sleep(world_rank);
        MPI_Send(msg, strlen(msg) + 1, MPI_CHAR, 0, MSG_TAG, MPI_COMM_WORLD);
    } else {
        printf("MPI: Greetings from %s, rank %d out of %d\n", processor_name, world_rank, world_size);
        for(i = 1; i < world_size; i++) {
            MPI_Recv(msg, MSG_LEN, MPI_CHAR, i, MSG_TAG, MPI_COMM_WORLD, &status);
            printf("%s\n", msg);
        }
    }

    MPI_Barrier(MPI_COMM_WORLD);
    sleep(world_rank);
    MPI_Barrier(MPI_COMM_WORLD);

#pragma omp parallel default(shared)
    {
        int np = omp_get_num_threads();
        int iam = omp_get_thread_num();
        printf("Hybrid: Hello from thread %d out of %d from process %d out of %d on %s\n",
                iam, np, world_rank, world_size, processor_name);
	sleep(1);
    }

    MPI_Finalize(); 

    return 0;
}
