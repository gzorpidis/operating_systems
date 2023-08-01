#pragma once

#include <semaphore.h>

struct transfered {
    char** data;
};

struct shared {
    sem_t* seg_sems;
    int* readCount;

    sem_t mutex;
    sem_t place_request;
    sem_t wait_response;
    int segmentRequested;
    int reqCounter;
    sem_t nextRequest;
};

typedef struct transfered* Transfered;
typedef struct shared* Shared;

// child processes call this function
void childProcess(int childID, uint numberOfRequests, int segmentCount, int segmentSize, Shared shm_ptr, Transfered buffer, int maxStringLength, int lastSegmentSize);

// Select a random integer in [l,r]
int generate_in_interval(int l, int r);
