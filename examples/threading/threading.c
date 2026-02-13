#include "threading.h"
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

// Optional: use these functions to add debug or error prints to your application
#define DEBUG_LOG(msg,...)
//#define DEBUG_LOG(msg,...) printf("threading: " msg "\n" , ##__VA_ARGS__)
#define ERROR_LOG(msg,...) printf("threading ERROR: " msg "\n" , ##__VA_ARGS__)

void* threadfunc(void* thread_param)
{

    // TODO: wait, obtain mutex, wait, release mutex as described by thread_data structure
    // hint: use a cast like the one below to obtain thread arguments from your parameter
    //struct thread_data* thread_func_args = (struct thread_data *) thread_param;

    if(thread_param == NULL)
        return NULL;

    struct thread_data* thread_func_args =
        (struct thread_data *) thread_param;

    thread_func_args->thread_complete_success = false;

    // Wait before attempting to obtain mutex
    usleep(thread_func_args->wait_to_obtain_ms * 1000);

    // Lock mutex
    if(pthread_mutex_lock(thread_func_args->mutex) != 0) {
        ERROR_LOG("Failed to lock mutex");
        return thread_param;
    }

    // Hold mutex for specified time
    usleep(thread_func_args->wait_to_release_ms * 1000);

    // Unlock mutex
    if(pthread_mutex_unlock(thread_func_args->mutex) != 0) {
        ERROR_LOG("Failed to unlock mutex");
        return thread_param;
    }

    thread_func_args->thread_complete_success = true;

    return thread_param;
}


bool start_thread_obtaining_mutex(pthread_t *thread, pthread_mutex_t *mutex,int wait_to_obtain_ms, int wait_to_release_ms)
{
    /**
     * TODO: allocate memory for thread_data, setup mutex and wait arguments, pass thread_data to created thread
     * using threadfunc() as entry point.
     *
     * return true if successful.
     *
     * See implementation details in threading.h file comment block
     */

    if(thread == NULL || mutex == NULL)
        return false;

    // Allocate thread_data dynamically
    struct thread_data *thread_args =
        malloc(sizeof(struct thread_data));

    if(thread_args == NULL) {
        ERROR_LOG("Failed to allocate memory for thread_data");
        return false;
    }

    // Initialize structure
    thread_args->mutex = mutex;
    thread_args->wait_to_obtain_ms = wait_to_obtain_ms;
    thread_args->wait_to_release_ms = wait_to_release_ms;
    thread_args->thread_complete_success = false;

    // Create thread
    if(pthread_create(thread, NULL, threadfunc, thread_args) != 0) {
        ERROR_LOG("Failed to create thread");
        free(thread_args);
        return false;
    }

    return true;
}

