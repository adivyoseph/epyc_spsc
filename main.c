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
#define SEND_CNT 10


typedef struct {
    int msg;// __attribute__ ((aligned(CACHELINE_SIZE)));
} msg_t ;


typedef struct {
    int capacity;
    int writeIndex __attribute__ ((aligned(CACHELINE_SIZE)));
    int readCache  __attribute__ ((aligned(CACHELINE_SIZE)));
    int readIndex __attribute__ ((aligned(CACHELINE_SIZE)));
    int writeCache  __attribute__ ((aligned(CACHELINE_SIZE)));
    int id __attribute__ ((aligned(CACHELINE_SIZE)));
    msg_t msg[FIFO_DEPTH_MAX+ 2] __attribute__ ((aligned(CACHELINE_SIZE)));

} spscq_t __attribute__ ((aligned(CACHELINE_SIZE)));

spscq_t workqs[2];


typedef struct {
    pthread_t   threadId;
    int         cpuAffinity;
    spscq_t*    spscqIn;
    spscq_t*    spscqOut;
    int         id;
    int         count;
} context_t;

context_t ctxs[2];
volatile int g_sendStart = 0;

void usage();
void workq_init(spscq_t* wq, int size,int id);
int workq_write(spscq_t* wq, int msg);
int workq_read(spscq_t* wq);
void *send_func(void *p_arg);
void *loopback_func(void *p_arg);

int main(int argc, char **argv) {
    int opt;            //for commandline options
    int cpu_cli = 0;
    int cpu_send = 2;
    int cpu_loopback = 4;
    cpu_set_t my_set;   // Define your cpu_set bit mask. 
    struct timespec start;
    struct timespec end;
    double accum;
    int x, rc;

     
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
        case 'b':
            cpu_send = atoi(optarg);
            //printf("cpu_cli %d\n", cpu_cli); 
            break;  
        case 'c':
            cpu_loopback = atoi(optarg);
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
    
    workq_init(&workqs[0], 512, 0);
    workq_init(&workqs[1], 512, 1);

    //thread init
    ctxs[0].id = 0;
    ctxs[0].cpuAffinity = cpu_send;
    ctxs[0].spscqOut = &workqs[0];
    ctxs[0].count = SEND_CNT;

    ctxs[1].id = 1;
    ctxs[1].cpuAffinity = cpu_loopback;
    ctxs[1].spscqIn = &workqs[0];
    ctxs[1].spscqOut = &workqs[1];
    ctxs[1].count = SEND_CNT;


    CPU_ZERO(&my_set); 
    CPU_SET(cpu_cli, &my_set);
    sched_setaffinity(0, sizeof(cpu_set_t), &my_set);
   
    pthread_create(&ctxs[0].threadId, NULL, send_func, (void *) &ctxs[0]);

    pthread_create(&ctxs[1].threadId, NULL, loopback_func, (void *) &ctxs[1]);

    clock_gettime(CLOCK_REALTIME, &start);
    g_sendStart = 1;
    for (x = 0; x < SEND_CNT; x++) {
        do {
            rc = workq_read(&workqs[1]);
        } while (rc < 0);
        //printf("rx %d msg %d\n", x, rc);
    }

    clock_gettime(CLOCK_REALTIME, &end);

    accum = ( end.tv_sec - start.tv_sec ) + (double)( end.tv_nsec - start.tv_nsec ) / (double)BILLION;
    printf( "%lf\n", accum );

    pthread_join(ctxs[0].threadId, NULL);
    pthread_join(ctxs[1].threadId, NULL);
    return 0;
}

void usage(){
    printf("-h     help\n-a    cli_cpu\n-b    send cpu\n-c    loopback cpu\n");
}


void workq_init(spscq_t* wq, int size, int id){
    wq->capacity = size;
    wq->id = id;
}

int workq_write(spscq_t* wq, int msg) {
    int nextWriteIndex = wq->writeIndex +1 ;
    if(nextWriteIndex == wq->capacity){
        nextWriteIndex = 0;
    }
    if(nextWriteIndex == wq->readCache){
        wq->readCache = wq->readIndex;
        if(nextWriteIndex == wq->readCache) return 0;
    }
    wq->msg[wq->writeIndex].msg = msg;
    //printf("workq_write_%d [%d]msg %d\n", wq->id, wq->writeIndex, wq->msg[wq->writeIndex].msg);
    wq->writeIndex = nextWriteIndex;

    return 1;
}

int workq_read(spscq_t* wq) {
    int msg;
    int nextReadIndex;
    if(wq->readIndex== wq->writeCache){
        wq->writeCache = wq->writeIndex;
        if(wq->writeCache == wq->readIndex){
            return -1;
        }
    }
    msg = wq->msg[wq->readIndex].msg;
    //printf("workq_read_%d [%d]msg %d\n", wq->id, wq->readIndex, wq->msg[wq->readIndex].msg);
    nextReadIndex = wq->readIndex + 1;
    if(nextReadIndex == wq->capacity){
        wq->readIndex = 0;
    }
    wq->readIndex =nextReadIndex;
    return msg;

}


void *send_func(void *p_arg){
    context_t *this = (context_t *) p_arg;
    //unsigned cpu, numa;
    cpu_set_t my_set;        /* Define your cpu_set bit mask. */
    int send_cnt = 0;
    int send_req = this->count;
    printf("Thread_%d PID %d %d\n", this->id, getpid(), gettid());


    CPU_ZERO(&my_set); 
    CPU_SET(this->cpuAffinity, &my_set);
    sched_setaffinity(0, sizeof(cpu_set_t), &my_set);
    
    //wait for start command
    while (g_sendStart == 0){
      
    };
    printf("Thread_%d started\n", this->id);
    while (send_cnt < send_req){
      
       if (workq_write(this->spscqOut, send_cnt) > 0){
            // printf("send msg %d\n", send_cnt);
            send_cnt++;
            
        }
    };
    return NULL;
}


void *loopback_func(void *p_arg){
    context_t *this = (context_t *) p_arg;
    //unsigned cpu, numa;
    cpu_set_t my_set;        /* Define your cpu_set bit mask. */
    int msg, rc;
    int send_cnt = 0;
    int send_req = this->count;
    
    printf("Thread_%d PID %d %d\n", this->id, getpid(), gettid());


    CPU_ZERO(&my_set); 
    CPU_SET(this->cpuAffinity, &my_set);
    sched_setaffinity(0, sizeof(cpu_set_t), &my_set);
    
    printf("Thread_%d started\n", this->id);
    while (send_cnt < send_req){
      
       msg = workq_read(this->spscqIn);
       if (msg >= 0){
       // printf("lb %d msg %d\n", send_cnt, msg);
        do {
            rc = workq_write(this->spscqOut, msg);
        } while (rc <= 0);
        send_cnt++;
        //printf("lb-> %d\n", send_cnt);
       }
      
    }
    return NULL;
}
