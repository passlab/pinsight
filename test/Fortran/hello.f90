program Hello
  use omp_lib, only: omp_get_thread_num, omp_get_num_threads

  implicit none

  integer :: thread_id
  integer :: nthreads

  print *, "Entering parallel region"
  nthreads = 2

  !$omp parallel private( thread_id, nthreads )

  ! ID of the thread in the current team
  thread_id = omp_get_thread_num()
  ! Number of threads in the current team
  nthreads = omp_get_num_threads()

  print *, "I'm thread", thread_id, "out of", nthreads, "threads."
  !$omp end parallel
  print *, "Exit from parallel region"
end program Hello
