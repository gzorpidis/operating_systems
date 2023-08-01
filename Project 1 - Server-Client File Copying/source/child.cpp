
#include <stdlib.h>
#include <iostream>
#include <unistd.h>
#include <sys/mman.h>
#include <string>
#include <string.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/wait.h>
#include <semaphore.h>
#include <pthread.h>
#include <fcntl.h> 
#include <assert.h>
#include <errno.h>
#include <sys/time.h>
#include "../include/shared_memory.hpp"



void childProcess(int childID, uint numberOfRequests, int segmentCount, int segmentSize, Shared shm_ptr, Transfered buffer, int maxStringLength, int lastSegmentSize) {
    
    int selectedSegment;
    int selectedLine;

    // segmentCount => How many segments we have available
    // segmentSize => How many lines every segment holds (except maybe the last one)
    // lastSegmentSize => How many lines the last segment has (if segmentation produces a last segment which is smaller)
    
    // Create the child log file
    FILE * fptr;
    int length = snprintf( NULL, 0, "%d", childID );
    char* childProcessID = (char*) malloc(sizeof(char) * (length + 1));
    snprintf( childProcessID, length + 1, "%d", childID );
    char fileName[80];
    strcpy(fileName, "child_");
    strcat(fileName, childProcessID);
    free(childProcessID);
    char extension[5] = ".txt";
    strcat(fileName, extension);
    if ( (fptr = fopen(fileName, "w")) == NULL) {
        fprintf(stderr, "Problem creating child log file");
        exit (EXIT_FAILURE);
    }

    char* to_read_into = (char*) malloc(sizeof(char) * (maxStringLength + 1));
    unsigned int curtime = time(NULL);
    srand((unsigned int) curtime - getpid());

    uint j = 0;
    while (j < numberOfRequests) {
        
        // Select segment in [1, segmentCount]
        if (j == 0) {
            selectedSegment = generate_in_interval(1, segmentCount);
        } else {
            double probabilityOfSegmentChange = 0.3;
            double result = rand() / RAND_MAX;
            if (result < probabilityOfSegmentChange) {
                int nextSegment;
                while ( ( nextSegment = generate_in_interval(1, segmentCount)) ==  selectedSegment);
                selectedSegment = nextSegment;
            }
        }

        // Select a random line in [1, segmentSize]

        // If we have selected the very last available segment
        if (selectedSegment == segmentCount) {
            // If lastSegmentSize is 0, we don't have a degenerate segment
            // else we have to select a smaller range, so we don't to out of bounds
            if (lastSegmentSize != 0)
                selectedLine = generate_in_interval(1, lastSegmentSize);
            else
                selectedLine = generate_in_interval(1, segmentSize);
        } else {
            // Else normally select between all the lines available
            selectedLine = generate_in_interval(1, segmentSize);
        }
            

        struct timeval start, end;
        
        gettimeofday(&start, 0);

        // Now we successfully have obtained a pair of segment and line
        sem_wait(&(shm_ptr->seg_sems[selectedSegment-1]));
            shm_ptr->readCount[selectedSegment-1]++;
            if (shm_ptr->readCount[selectedSegment-1] == 1) {
                sem_wait(&shm_ptr->mutex);

                // Place request
                shm_ptr->segmentRequested = selectedSegment;
                sem_post(&(shm_ptr->place_request));
                sem_wait(&(shm_ptr->wait_response));

            }
            int old_req = shm_ptr->reqCounter;
            shm_ptr->reqCounter++; 
            assert(shm_ptr->reqCounter == old_req + 1);
            
            assert(shm_ptr->segmentRequested == selectedSegment);

        sem_post(&(shm_ptr->seg_sems[selectedSegment-1]));

        // Read from shared memory
        strcpy(to_read_into, buffer->data[selectedLine-1]);
        gettimeofday(&end, 0);

        usleep(20000);

        sem_wait(&(shm_ptr->seg_sems[selectedSegment-1]));

            // Get the milliseconds
            uint64_t millis_start = (start.tv_sec * (uint64_t)1000) + (start.tv_usec / 1000);
            uint64_t millis_end = (end.tv_sec * (uint64_t)1000) + (end.tv_usec / 1000);

            // Output to the child log file
            fprintf(fptr, "%ld - %ld \t < %d, %d > - \t - %s\n", millis_start, millis_end , selectedSegment, selectedLine, buffer->data[selectedLine-1]);
            shm_ptr->readCount[selectedSegment-1]--;

            if (shm_ptr->readCount[selectedSegment-1] == 0) {
                // Interchange the segment
                sem_post(&shm_ptr->mutex);
                sem_post(&shm_ptr->nextRequest);
            }
        sem_post(&(shm_ptr->seg_sems[selectedSegment-1]));

        j++;    // Next child Request

    }
    // free the read_into string allocated at the start to place the desired line
    free(to_read_into);
    fclose(fptr);

    for(int i = 0; i < segmentSize; i++)
        shmdt(buffer->data[i]);
    
    shmdt(buffer->data);
    shmdt(buffer);

    shmdt(shm_ptr->readCount);
    shmdt(shm_ptr->seg_sems);
    shmdt(shm_ptr);

}