
#ifndef EX2OS_THREAD_H
#define EX2OS_THREAD_H

#include <stdio.h>
#include <setjmp.h>
//#include <stdlib.h>
#include <signal.h> //?
#include <unistd.h>

// Defines for this class
#define RUNNING 0
#define READY 1
#define BLOCKED 2
#define TERMINATED 3

class Thread
{
private:

    int __thread_id ;           // thread's ID number
    char *__thread_stack_p;    // thread's stack pointer
    int __thread_cs;            // thread's current state
    int __thread_priority;      // thread's priority
    int __num_of_quantum = 0;  // thread's quantum
    void (*f)(void);            // thread's code

public:

    /**
     * @brief this is the class constructor
     * @param f function pointer where the thread will begin his code
     * @param tid the thread it number
     * @param priority the priority that is given to particular thread
     */
    Thread(void (*f)(void), int tid, int priority);

    /**
     * @brief Thread deconstruction
     */
    ~Thread();

    /**
     * @return thread's ID number
     */
    int getId ();

    /**
     * @return thread's current state RUNNING/READY/BLOCKED
     */
    int getState();

    /**
     * @return the number of thread quantums (how many time this thread has been RUNNING)
     */
    int getQuantum();

    /**
     * @return the thread's priority number - 0 highest
     */
    int getPriority();

    /**
     * @param next_state changes thread's current state to RUNNING/READY/BLOCKED
     */
    void setState(int next_state);

    /**
     * @param new_priority changes the thread's priority number - 0 highest
     */
    void setPriority(int new_priority);

    /**
     * this function increases the number of thread quantums (how many time this thread has been RUNNING)
     */
    void incrQuantum();

    /**
     * this will use for thread switching -> save the mask, the 'PC' etc..
     */
    sigjmp_buf env;
};

#endif //EX2OS_THREAD_H
