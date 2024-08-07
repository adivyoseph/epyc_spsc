#define _GNU_SOURCE
#include <assert.h>
#include <sched.h> /* getcpu */
#include <stdio.h> 
#include <stdlib.h> 
#include <sys/types.h> 
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>   
#include <pthread.h> 
#include <signal.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <fcntl.h>
#include <sys/syscall.h>
#ifndef CACHELINE_SIZE
#define CACHELINE_SIZE 64
#endif
#define FIFO_DEPTH_MAX 1024
#define BILLION  1000000000L;


typedef struct {
    int msg;
} msg_t __attribute__ ((aligned(CACHELINE_SIZE)));


typedef struct {
    int capacity;
    int writeIndex __attribute__ ((aligned(CACHELINE_SIZE)));
    int readCache  __attribute__ ((aligned(CACHELINE_SIZE)));
    int readIndex __attribute__ ((aligned(CACHELINE_SIZE)));
    int writeCache  __attribute__ ((aligned(CACHELINE_SIZE)));
    int pad __attribute__ ((aligned(CACHELINE_SIZE)));
    msg_t msg[FIFO_DEPTH_MAX+ 2] __attribute__ ((aligned(CACHELINE_SIZE)));

} spscq_t __attribute__ ((aligned(CACHELINE_SIZE)));

spscq_t workqs[2];


typedef struct {
    pthread_t   threadId;
    int         cpuAffinity;
    spscq_t*    spscqIn;
    spscq_t*    spscqOut;
    int         id;
} context_t;

context_t ctxs[2];
volatile g_sendStart = 0;

void usage();
void workq_init(spscq_t* wq, int size);
int workq_write(spscq_t* wq, int msg);
int workq_read(spscq_t* wq);

int main(int argc, char **argv) {
    int opt;            //for commandline options
    int cpu_cli = 0;
    int cpu_send = 2;
    int cpu_loopback = 4;
    cpu_set_t my_set;   // Define your cpu_set bit mask. 
    struct timespec start;
    struct timespec end;
    double accum, accum1;
    int x;

     
    while((opt = getopt(argc, argv, "ha:b:c:")) != -1) 
    { 
        switch(opt) 
        { 
        case 'h':                   //help
            usage();
            return 0;
            break;
       
       case 'a':
            cpu_cli = atoi(optarg);
            //printf("cpu_cli %d\n", cpu_cli); 
            break; 


        default:
            return 1;
            break;
        }
    }
    printf("cpu_cli      %d\n", cpu_cli); 
    printf("cpu_send     %d\n", cpu_send); 
    printf("cpu_loopback %d\n", cpu_loopback); 
    
    workq_init(&workqs[0], 512);
    workq_init(&workqs[1], 512);

    //thread init
    ctxs[0].id = 0;
    ctxs[0].cpuAffinity = cpu_send;
    ctxs[0].spscqOut = &workqs[0];

    ctxs[1].id = 1;
    ctxs[1].cpuAffinity = cpu_loopback;
    ctxs[1].spscqIn = &workqs[0];
    ctxs[1].spscqOut = &workqs[1];


    CPU_ZERO(&my_set); 
    CPU_SET(cpu_cli, &my_set);
    sched_setaffinity(0, sizeof(cpu_set_t), &my_set);
   
    pthread_create(&ctxs[0].threadId, NULL, send_func, (void *) &ctxs[0]);

    pthread_create(&ctxs[1].threadId, NULL, loopback_func, (void *) &ctxs[1]);

    clock_gettime(CLOCK_REALTIME, &start);
    g_sendStart = 1;
    for (x = 0; x < 20000; x++) {

    }


    clock_gettime(CLOCK_REALTIME, &end);

    accum = ( end.tv_sec - start.tv_sec ) + (double)( end.tv_nsec - start.tv_nsec ) / (double)BILLION;
    printf( "%lf\n", accum );
    return 0;
}

void usage(){
    printf("-h     help\n-a    cli_cpu\n-b    send cpu\n-c    loopback cpu\n");
}


void workq_init(spscq_t* wq, int size){
    wq->capacity = size;
}

int workq_write(spscq_t* wq, int msg) {


}

int workq_read(spscq_t* wq) {


}