/*
 * User Level Threads Library (uthreads)
 * Authors: Alex Pritsker, Ofri Frederik Kamst
 */

#include "Threads.h"
#include "uthreads.h"
#include <iostream>
#include <stdlib.h>
#include <signal.h>
#include <vector>
#include <queue>
#include <list>
#include <sys/time.h>
#include <deque>
#include <map>

// Return values
#define SUCCESS 0
#define FAIL -1
#define TRUE 1
#define FALSE 0
// Magic numbers
#define MAIN_THREAD_ID 0
#define MAIN_THREAD_PRIORITY 0
// Messages
#define SYSTEM_ERROR_MSG "system error: "
#define USAGE_ERROR_MSG "thread library error: "

// Library Variables and Data Structures
static std::vector<Thread * > threads (MAX_THREAD_NUM);
static std::list<int> ready_threads;
static sigset_t signal_mask;
static int total_quantums = 0;
int * priority_array;
int priority_array_size;
int running_thread = 0;
static std::priority_queue <int ,std::vector<int>,std::greater<int>> tid_que;
static struct sigaction vt_handler;
static struct itimerval vt;


//--------------------------------------------
//------Internal Functions Declarations-------
//--------------------------------------------

/**
 * @return the next available tid
 */
int get_next_tid();

/**
 * @param tid
 * @return 1 if tid is valid and exists, otherwise 0
 */
int valid_tid(int tid);

/**
 * @param priority
 * @return 1 if priority is valid and exists, otherwise 0
 */
int valid_priority(int priority);

/**
 * Terminates single thread
 * @param tid
 */
void terminate_thread(int tid);

/**
 * Switches threads
 * @param mode the state the current thread (before switching) receives
 */
void switch_threads(int mode);

/**
 * Updates the current thread state, and counts total quantums
 * @param mode
 */
void switch_threads_helper(int mode);

/**
 * Sets the quantum of the tid
 * @param tid
 */
void set_virtual_timer(int tid);

/**
 * Handler function for SIVTALRM
 * @param sig_num
 */
void vt_handler_function(int sig_num);

/**
 * Sets the blocked signals
 */
void set_block_mask();

/**
 * Activates signal blocking
 */
void activate_mask();

/**
 * Deactivates signal blocking
 */
void deactivate_mask();

//--------------------------------------------
//------------External Interface--------------
//--------------------------------------------

/*
 * Description: This function initializes the thread library.
 * You may assume that this function is called before any other thread library
 * function, and that it is called exactly once. The input to the function is
 * an array of the length of a quantum in micro-seconds for each priority.
 * It is an error to call this function with an array containing non-positive integer.
 * size - is the size of the array.
 * Return value: On success, return 0. On failure, return -1.
*/
int uthread_init(int *quantum_usecs , int size)
{
    /* Input validity check */
    if (size <= 0)
    {
        std::cerr << USAGE_ERROR_MSG << "invalid size" << std::endl;
        return FAIL;
    }
    int i;
    for (i = 0 ; i < size ; i++)
    {
        if (quantum_usecs[i] <= 0)
        {
            std::cerr << USAGE_ERROR_MSG << "invalid quantums usecs" << std::endl;
            return FAIL;
        }
    }

    set_block_mask();
    activate_mask();

    /* Changing SIGVTALRM handler */
    vt_handler.sa_handler = &vt_handler_function;
    if (sigaction(SIGVTALRM,&vt_handler, nullptr) < 0)
    {
        std::cerr << SYSTEM_ERROR_MSG << "EFAULT or EINVAL from sigaction" << std::endl;
        deactivate_mask();
        return FAIL;
    }

    /* Initializations and Allocations */
    priority_array = new int (size);
    if (priority_array == nullptr)
    {
        return FAIL;
    }
    priority_array_size = size;
    for (i = 0; i < size; i++)
    {
        priority_array[i] = quantum_usecs[i];
    }
    for (i = 1 ;i < MAX_THREAD_NUM; i++)
    {
        tid_que.push(i);
    }
    Thread * main_thread = new (std::nothrow) Thread(nullptr, MAIN_THREAD_ID, MAIN_THREAD_PRIORITY);
    if (main_thread == nullptr)
    {
        std::cerr << SYSTEM_ERROR_MSG << "bad allocation" << std::endl;
        deactivate_mask();
        return FAIL;
    }
    main_thread->setState(RUNNING);
    threads.reserve(MAX_THREAD_NUM);
    for (i = 0; i < MAX_THREAD_NUM; i++)
    {
        threads[i] = nullptr;
    }
    threads[MAIN_THREAD_ID] = main_thread;
    threads[MAIN_THREAD_ID]->incrQuantum();
    total_quantums = 1;
    set_virtual_timer(MAIN_THREAD_ID);
    deactivate_mask();
    return SUCCESS;
}

/*
 * Description: This function creates a new thread, whose entry point is the
 * function f with the signature void f(void). The thread is added to the end
 * of the READY threads list. The uthread_spawn function should fail if it
 * would cause the number of concurrent threads to exceed the limit
 * (MAX_THREAD_NUM). Each thread should be allocated with a stack of size
 * STACK_SIZE bytes.
 * priority - The priority of the new thread.
 * Return value: On success, return the ID of the created thread.
 * On failure, return -1.
*/
int uthread_spawn(void(*f)(void) , int priority)
{
    activate_mask();
    if (f == nullptr || valid_priority(priority) == FALSE)
    {
        std::cerr << USAGE_ERROR_MSG << "function pointer or priority are invalid" << std::endl;
        deactivate_mask();
        return FAIL;
    }
    int tid = get_next_tid();
    if (tid == FAIL)
    {
        std::cerr << USAGE_ERROR_MSG << "threads overflow" << std::endl;
        deactivate_mask();
        return FAIL;
    }
    Thread * spawned_thread = new (std::nothrow) Thread (f, tid, priority);
    if (spawned_thread == nullptr)
    {
        std::cerr << SYSTEM_ERROR_MSG << "bad allocation" << std::endl;
        deactivate_mask();
        return FAIL;
    }
    threads[tid] = spawned_thread ;
    threads[tid]->setState(READY);
    ready_threads.push_back(tid);
    deactivate_mask();
    return tid;
}

/*
 * Description: This function changes the priority of the thread with ID tid.
 * If this is the current running thread, the effect should take place only the
 * next time the thread gets scheduled.
 * Return value: On success, return 0. On failure, return -1.
*/
int uthread_change_priority(int tid , int priority)
{
    activate_mask();
    if(!valid_priority(priority) || !valid_tid(tid))
    {
        std::cerr << USAGE_ERROR_MSG << "invalid tid or priority" << std::endl;
        deactivate_mask();
        return FAIL;
    }
    threads[tid]->setPriority(priority);
    deactivate_mask();
    return SUCCESS;
}

/*
 * Description: This function terminates the thread with ID tid and deletes
 * it from all relevant control structures. All the resources allocated by
 * the library for this thread should be released. If no thread with ID tid
 * exists it is considered an error. Terminating the main thread
 * (tid == 0) will result in the termination of the entire process using
 * exit(0) [after releasing the assigned library memory].
 * Return value: The function returns 0 if the thread was successfully
 * terminated and -1 otherwise. If a thread terminates itself or the main
 * thread is terminated, the function does not return.
*/
int uthread_terminate(int tid)
{
    activate_mask();
    if (!valid_tid(tid))
    {
        std::cerr << USAGE_ERROR_MSG << "invalid tid" << std::endl;
        deactivate_mask();
        return FAIL;
    }
    if (tid == MAIN_THREAD_ID)
    {
        int i;
        for (i = 1; i < MAX_THREAD_NUM; i++)
        {
            if (threads[i] != nullptr)
            {
                terminate_thread(i);
            }
        }
        delete [] priority_array;
        deactivate_mask();
        exit(0);
    }
    else if (tid == running_thread)
    {
        switch_threads_helper(BLOCKED);
        terminate_thread(tid);
        deactivate_mask();
        siglongjmp(threads[running_thread]->env, 5);
    }
    else
    {
        terminate_thread(tid);
        deactivate_mask();
        return SUCCESS ;
    }
}

/*
 * Description: This function blocks the thread with ID tid. The thread may
 * be resumed later using uthread_resume. If no thread with ID tid exists it
 * is considered as an error. In addition, it is an error to try blocking the
 * main thread (tid == 0). If a thread blocks itself, a scheduling decision
 * should be made. Blocking a thread in BLOCKED state has no
 * effect and is not considered an error.
 * Return value: On success, return 0. On failure, return -1.
*/
int uthread_resume(int tid)
{
    activate_mask();
    if (!valid_tid(tid))
    {
        std::cerr << USAGE_ERROR_MSG << "invalid tid" << std::endl;
        deactivate_mask();
        return FAIL;
    }
    if(threads[tid]->getState() == BLOCKED)
    {
        threads[tid]->setState(READY);
        ready_threads.push_back(tid);
    }
    deactivate_mask();
    return SUCCESS;
}

/*
 * Description: This function resumes a blocked thread with ID tid and moves
 * it to the READY state. Resuming a thread in a RUNNING or READY state
 * has no effect and is not considered as an error. If no thread with
 * ID tid exists it is considered an error.
 * Return value: On success, return 0. On failure, return -1.
*/
int uthread_block(int tid)
{
    activate_mask();
    if (!valid_tid(tid) || tid == MAIN_THREAD_ID)
    {
        std::cerr << USAGE_ERROR_MSG << "invalid tid" << std::endl;
        deactivate_mask();
        return FAIL;
    }
    if (threads[tid]->getState() == READY)
    {
        ready_threads.remove(tid);
        threads[tid]->setState(BLOCKED);
    }
    if (tid == running_thread)
    {
        switch_threads(BLOCKED);
    }
    deactivate_mask();
    return SUCCESS;
}

/*
 * Description: This function returns the thread ID of the calling thread.
 * Return value: The ID of the calling thread.
*/
int uthread_get_tid()
{
    return running_thread;
}

/*
 * Description: This function returns the total number of quantums since
 * the library was initialized, including the current quantum.
 * Right after the call to uthread_init, the value should be 1.
 * Each time a new quantum starts, regardless of the reason, this number
 * should be increased by 1.
 * Return value: The total number of quantums.
*/
int uthread_get_total_quantums()
{
    return total_quantums;
}

/*
 * Description: This function returns the number of quantums the thread with
 * ID tid was in RUNNING state. On the first time a thread runs, the function
 * should return 1. Every additional quantum that the thread starts should
 * increase this value by 1 (so if the thread with ID tid is in RUNNING state
 * when this function is called, include also the current quantum). If no
 * thread with ID tid exists it is considered an error.
 * Return value: On success, return the number of quantums of the thread with ID tid.
 * 			     On failure, return -1.
*/
int uthread_get_quantums(int tid)
{
    activate_mask();
    if (valid_tid(tid) == FALSE) {
        std::cerr << USAGE_ERROR_MSG << "invalid tid" << std::endl;
        deactivate_mask();
        return FAIL;
    }
    deactivate_mask();
    return threads[tid]->getQuantum();
}

//--------------------------------------------
//-----Internal Functions Implementations-----
//--------------------------------------------

/**
 * @return the next available tid
 */
int get_next_tid()
{
    if (tid_que.empty())
        return FAIL;
    int tid = tid_que.top();
    tid_que.pop();
    return tid;
}

/**
 * @param tid
 * @return 1 if tid is valid and exists, otherwise 0
 */
int valid_tid(int tid)
{
    if (tid >= 0 && tid < MAX_THREAD_NUM && threads[tid] != nullptr)
    {
        return TRUE;
    }
    return FALSE;
}

/**
 * Terminates single thread
 * @param tid
 */
void terminate_thread(int tid)
{
    if(threads[tid]->getState() == READY)
    {
        ready_threads.remove(tid);
    }
    delete threads[tid];
    threads[tid] = nullptr;
    tid_que.push(tid);
}

/**
 * @param priority
 * @return 1 if priority is valid and exists, otherwise 0
 */
int valid_priority(int priority)
{
    if (priority >= 0 && priority < priority_array_size)
    {
        return TRUE;
    }
    return FALSE;
}

/**
 * Sets the blocked signals
 */
void set_block_mask()
{
    sigemptyset(&signal_mask);
    sigaddset(&signal_mask,SIGVTALRM);
}

/**
 * Activates signal blocking
 */
void activate_mask()
{
    sigprocmask(SIG_BLOCK,&signal_mask, nullptr);
}

/**
 * Deactivates signal blocking
 */
void deactivate_mask()
{
    sigprocmask(SIG_UNBLOCK,&signal_mask, nullptr);
}

/**
 * Sets the quantum of the tid
 * @param tid
 */
void set_virtual_timer(int tid)
{
    int priority = threads[tid]->getPriority();
    int vt_usec = priority_array[priority];
    vt.it_value.tv_sec = 0;
    vt.it_value.tv_usec = vt_usec;
    vt.it_interval.tv_sec = 0;
    vt.it_interval.tv_usec = 0;
    if(setitimer(ITIMER_VIRTUAL, &vt, nullptr))
    {
        std::cerr << SYSTEM_ERROR_MSG << "EFAULT or EINVAL from setitimer" << std::endl;
        exit(1);
    }
}

/**
 * Handler function for SIVTALRM
 * @param sig_num
 */
void vt_handler_function(int sig_num)
{
    switch_threads(READY);
}

/**
 * Updates the current thread state, and counts total quantums
 * @param mode
 */
void switch_threads_helper(int mode)
{
    if (mode != BLOCKED)
    {
        threads[running_thread]->setState(READY);
        ready_threads.push_back(running_thread);
    }
    else
    {
        threads[running_thread]->setState(BLOCKED);
    }
    running_thread = ready_threads.front();
    ready_threads.pop_front();
    total_quantums++;
    threads[running_thread]->incrQuantum();
    threads[running_thread]->setState(RUNNING);
    set_virtual_timer(running_thread);
}

/**
 * Switches threads
 * @param mode the state the current thread (before switching) receives
 */
void switch_threads(int mode)
{
    activate_mask();
    int ret_val = sigsetjmp(threads[running_thread]->env, 1);
    if(ret_val == 5)
    {
        deactivate_mask();
        return;
    }
    switch_threads_helper(mode);
    deactivate_mask();
    siglongjmp(threads[running_thread]->env, 5);
}
