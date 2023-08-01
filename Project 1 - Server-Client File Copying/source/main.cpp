

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

int generate_in_interval(int l, int r) {
    return (rand() % (r-l+1)) + 1;
}


using namespace std;


int main(int argc, char* argv[]) {

    Shared shm_ptr = NULL; 
    int N; 
    int segmentSize = 0; 
    int childRequests = 0; 
    int shmID = 0; 
    int segmentCount = 0; 
    int i = 0; 
    char* inputFile = NULL; 

    srand(time(NULL));

    if (argc != 5) {
        fprintf(stdout, "usage: <main> <input_file> <number of children processes> <segmentSize> <childRequests>\n");
        return 0;
    } else if (argc == 5) {
        // Read input arguments
        inputFile = argv[1]; 
        N = atoi(argv[2]); 
        segmentSize = atoi(argv[3]); 
        childRequests = atoi(argv[4]);
    }
    

    // Initial read of file to
    // get the line_counter => number of lines
    // max_line_length => the length of the largest line in the file

    FILE *fin;
    if ((fin = fopen((const char*) inputFile,"r")) == NULL){
       printf("Error! opening file");
       exit(1);
    }
    char * line = NULL;
    size_t len = 0;
    ssize_t read;
    ssize_t max_line_length = 0;
    size_t line_counter = 0;

    // Get max line length and lines in file
    while ((read = getline(&line, &len, fin)) != -1) {
        if (read > max_line_length)
            max_line_length = read; 
        line_counter++;
    }

    fclose(fin);

    // According to the segmentation size (segmentSize)
    // We might have the last segment to be smaller
    // than the other ones
    int lastSegmentSize;
    if (line_counter % segmentSize == 0) {
        segmentCount = line_counter / segmentSize;
        lastSegmentSize = 0;
    } else {
        segmentCount = (line_counter / segmentSize) + 1;
        lastSegmentSize = line_counter % segmentSize;
    }


    if ( (shmID = shmget(IPC_PRIVATE, sizeof(struct shared), 0777 | IPC_CREAT)) == -1 ) {
        perror("Failed to create shared memory segment");
        return 1;
    }

    if ( (shm_ptr = (Shared) shmat(shmID, NULL, 0)) == (void*) -1 ) {
        perror("Failed to attach memory segment");
        return 1;
    }
    
    sem_init(&(shm_ptr->mutex), 1 , 1);
    sem_init(&(shm_ptr->place_request), 1, 0);
    sem_init(&(shm_ptr->wait_response), 1 , 0);
    sem_init(&(shm_ptr->nextRequest), 1, 0);

    // Initialize sem_t* seg_sems 
    key_t seg_shm_ID;

    seg_shm_ID = shmget(IPC_PRIVATE, sizeof(sem_t) * segmentCount, 0777 | IPC_CREAT);
    assert(segmentCount != 0);
    if (seg_shm_ID == -1) {  perror("Failed to create shared memory segment"); return 1; }
    if ( ( shm_ptr->seg_sems = (sem_t *) shmat(seg_shm_ID, NULL, 0) ) == (void*) -1 ) {
        perror("Could not get shared memory location");
        return EXIT_FAILURE;
    }

    for(int i = 0; i < segmentCount; i++) sem_init(&(shm_ptr->seg_sems[i]), 1, 1);


    // Initialize the readCount array
    key_t readID = shmget(IPC_PRIVATE, sizeof(int) * segmentCount, 0777 | IPC_CREAT);
    shm_ptr->readCount = (int*) shmat(readID, NULL, 0);
    for (int i = 0; i < segmentCount; i++) shm_ptr->readCount[i] = 0;

    // Output buffer, has the segment saved
    key_t bufferID = shmget(IPC_PRIVATE, sizeof(struct transfered), 0777 | IPC_CREAT);
    Transfered DATA_IN = (Transfered) shmat(bufferID, NULL, 0); 

    key_t dataID = shmget(IPC_PRIVATE, sizeof(char*) * (segmentSize), 0777 | IPC_CREAT);
    if (dataID == -1) { perror("Failed to create shared memory segment"); return 1; }
    if ( (DATA_IN->data = (char**) shmat(dataID, NULL, 0) ) == (void*) -1 ) {
        perror("Could not get shared memory location");
        return EXIT_FAILURE;
    }

    key_t* keys_inside = (key_t *) malloc(sizeof(key_t) * (segmentSize));

    for(size_t i = 0; i < segmentSize; i++) {
        keys_inside[i] = (key_t) shmget(IPC_PRIVATE, sizeof(char) * (max_line_length + 1), 0777 | IPC_CREAT);
        if (keys_inside[i] == -1 ) {
            perror("Failed to create shared memory segment");
            return 1;
        }
        if ( (DATA_IN->data[i] = (char*) shmat( keys_inside[i], NULL, 0) ) == (void*) -1 ) {
            perror("Could not get shared memory location");
            return EXIT_FAILURE;
        }
    }

    shm_ptr->reqCounter = 0;
    assert(shm_ptr->reqCounter == 0);
    int sem_value;
    sem_getvalue(&shm_ptr->mutex, &sem_value);
    assert(sem_value == 1);
    sem_getvalue(&shm_ptr->place_request, &sem_value);
    assert(sem_value == 0);
    sem_getvalue(&shm_ptr->wait_response, &sem_value);
    assert(sem_value == 0 );
    sem_getvalue(&shm_ptr->nextRequest, &sem_value);
    assert(sem_value == 0);

    // Create child processes
    for (i = 0; i < N; i++)
        if (fork() == 0) {
            childProcess(i, childRequests, segmentCount, segmentSize, shm_ptr, DATA_IN, max_line_length, lastSegmentSize); 
            exit(0);
        }
    

    int lastRequested = -1;
    FILE* fptr = fopen("log_main.txt", "w");
    if (fptr == NULL) {
        fprintf(stderr, "Problem creating main log file\n");
        exit (EXIT_FAILURE);
    }

    time_t segmentIn;
    time_t segmentOut = 1;

    while ( shm_ptr->reqCounter != N * childRequests ) {
        
        sem_wait(&(shm_ptr->place_request));
        // Now at parent process, to check for requested segment
        bool repeat = lastRequested != shm_ptr->segmentRequested || lastRequested == -1;
        
        if (repeat) {
            // Here only when we switch the 
            // desired segment

            if (lastRequested == -1) {
                segmentIn = time(NULL);
            } else {
                segmentOut = time(NULL);
                // Output to the log file, since the previous segment will now be leaving the shared memory
                fprintf(fptr, "Segment: %d \t In: %ld - \t Out: %ld\n", lastRequested, segmentIn, segmentOut);
                segmentIn = time(NULL);
            }

            lastRequested = shm_ptr->segmentRequested;
            uint k = 0;
            FILE *fi;
            if ((fi = fopen((const char*) inputFile,"r")) == NULL){
               printf("Error! opening file");
               exit(1);
            }

            char* string_read = (char*) malloc(sizeof(char) * (max_line_length+1) );

            uint lineStarting = 1 + segmentSize * (shm_ptr->segmentRequested-1);
            int j = 0;
            int limit = 0;

            if (shm_ptr->segmentRequested == segmentCount) {
                // Last segment requested
                if (lastSegmentSize != 0)
                    limit = lastSegmentSize;
                else
                    limit = segmentSize;
            } else {
                limit = segmentSize;
            }

            while (fgets(string_read, max_line_length+1, fi) != NULL) {
                if (k + 1 >= lineStarting && k + 1 < lineStarting + limit) {
                    assert (strlen(string_read) > 0);
                    if (string_read[strlen(string_read)-1] == '\n')
                        string_read[strlen(string_read)-1] = '\0';
                    strncpy(DATA_IN->data[j], string_read, max_line_length + 1);
                    j++;
                }
                if (k + 1 >= lineStarting + segmentSize) break;
                k++;
            }
            free(string_read);
            fclose(fi);
        } else {
            // Intentionally left here, show that we don't do anything for a segment already present there
            // In case we have implemented it in a way such that no
            // printf("Segment %d requested is already present in shared memory\n", shm_ptr->segmentRequested);
        }
        sem_post(&(shm_ptr->wait_response));

        // Wait for the last request of a child to tell inform us to wait
        // for another segment to come
        sem_wait(&(shm_ptr->nextRequest));
    }

    // Logfile last line 
    if (segmentOut != 1)
        fprintf(fptr, "Segment: %d \t In: %ld - \t Out: %ld\n", lastRequested, segmentIn, segmentOut);
    else
        fprintf(fptr, "Segment: %d \t In: %ld - \t Out: NA", lastRequested, segmentIn);

    // Parent waits for all child processes to finish 
    for (i = 0; i < N; i++)
        wait(0);
    
    
    fclose(fptr);


    // Very important to close the semaphore and remove it
    cout << "\t END \t" << endl;

    sem_destroy(&(shm_ptr->mutex));
    sem_destroy(&(shm_ptr->place_request));
    sem_destroy(&(shm_ptr->wait_response));
    sem_destroy(&(shm_ptr->nextRequest));

    shmdt(shm_ptr->readCount);
    shmctl(readID, IPC_RMID, 0);

    for (int i = 0; i < segmentCount; i++) 
        sem_destroy(&(shm_ptr->seg_sems[i]));

    shmdt(shm_ptr->seg_sems);
    shmctl(seg_shm_ID, IPC_RMID, 0);

    shmdt(shm_ptr);
    shmctl(shmID, IPC_RMID, 0);
    
    for(size_t i = 0; i < segmentSize; i++) {
        shmdt(DATA_IN->data[i]);
        shmctl(keys_inside[i], IPC_RMID, 0);
    }
    free(keys_inside);
    free(line);
    
    shmdt(DATA_IN->data);
    shmctl(dataID, IPC_RMID, 0); 

    shmdt(DATA_IN);
    shmctl(bufferID, IPC_RMID, 0);

    return 0;
}