#include<pthread.h>
#include<stdio.h>
#include<stdlib.h>
#include<semaphore.h>
 
#define NUMBER_OF_THREADS 10

sem_t binSem;

static void *print_hello_world(void *tid)
{
while(1){
	sem_wait(&binSem);
		printf("hello world.Grettings from thread%lu\n",pthread_self());}
}

int main()
{
		pthread_t threads;
		int status;
		
      // Result for System call
    int res = 0;

     // Initialize semaphore
     res = sem_init(&binSem, 0, 0);
    if (res) {
         printf("Semaphore initialization failed!!/n");
         exit(EXIT_FAILURE);
     }
	
	printf("main here.Creating thread\n");
	status=pthread_create(&threads,NULL,print_hello_world,NULL);

	if(status!=0)
	{
		printf("Oops.pthread_create return error code%d",status);
		exit(-1);
	}
 
	
	sem_post(&binSem);
	sleep(1);
	sem_post(&binSem);
	sleep(1);
	sem_destroy(&binSem);

	exit(0);
}

