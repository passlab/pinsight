#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <math.h>
#define NUM_THREADS 4 
#define NUM_ITERATIONS 10
#define TEST_SUSPEND 1

#define TRACEPOINT_CREATE_PROBES
#define TRACEPOINT_DEFINE
#include "yield_suspend_tp.h"

typedef struct thread_t {
	pthread_t pthread;
	unsigned short id;
	unsigned short suspended;
	pthread_mutex_t mutex;
	pthread_cond_t  cond;
} thread_t;

/**
 * A thread can only suspend itself
 */
void pthread_suspend(thread_t *t) {
    pthread_mutex_lock(&t->mutex);
    t->suspended = 1;
    pthread_cond_wait(&t->cond, &t->mutex);
    t->suspended = 0;
    pthread_mutex_unlock(&t->mutex);
}

void pthread_resume(thread_t *t) {
    pthread_cond_signal(&t->cond);
}

void *thread_func(void *t) { 
    int i;
    thread_t *thread = (thread_t*)t;
    pthread_mutex_init(&thread->mutex, NULL);
    pthread_cond_init(&thread->cond, NULL);
    thread->suspended = 0;
    double result=0.0;
    printf("Thread %d starting...\n",thread->id);
    for (i=0; i<NUM_ITERATIONS; i++)  { 
        printf("Thread %d at iteration %d\n",thread->id, i);
        result = result + sin(i) * tan(i); 
    	tracepoint(yield_suspend, before_yield_tp, thread->id, i);
    	sched_yield();
    	tracepoint(yield_suspend, after_yield_tp, thread->id, i);

    	tracepoint(yield_suspend, before_sleep_tp, thread->id, i);
	sleep(thread->id+1);
    	tracepoint(yield_suspend, after_sleep_tp, thread->id, i);

#ifdef TEST_SUSPEND
    	tracepoint(yield_suspend, before_suspended_tp, thread->id, i);
	pthread_suspend(thread);
    	tracepoint(yield_suspend, after_suspended_tp, thread->id, i);
#endif
    }
    printf("Thread %d done. Result = %e\n",thread->id, result); 
    pthread_exit((void*) t);  
} 

int main (int argc, char *argv[])
{
   thread_t threads[NUM_THREADS];
   pthread_attr_t attr;
   unsigned short t;
   int i;
   void *status;

   /* Initialize and set thread detached attribute */
   pthread_attr_init(&attr);
   pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);

   for(t=0; t<NUM_THREADS; t++) {
      printf("Main: creating thread %d\n", t);
      threads[t].id = t;
      pthread_create(&threads[t].pthread, &attr, thread_func, &threads[t]);
   }

#ifdef TEST_SUSPEND
   double result = 0.0;
   for (i=0; i<NUM_ITERATIONS; i++) {
       result = result + sin(i) * tan(i); 
       for(t=0; t<NUM_THREADS; t++) {
	   sleep(t+1);
           pthread_resume(&threads[t]);
       }
   }
#endif
   /* Free attribute and wait for the other threads */
   pthread_attr_destroy(&attr);
   for(t=0; t<NUM_THREADS; t++) {
      pthread_join(threads[t].pthread, &status);
      printf("Main: joined with thread %d, status: %ld\n", t, (long)status);
   }
   printf("Main: program completed. Exiting.\n");
   return 0;
}
