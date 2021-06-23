//
// Created by Alex on 20/04/2020.
//

#include "Threads.h"
#include <stdlib.h>
#include <iostream>
#include <setjmp.h>
#include <signal.h>

#define STACK_SIZE 4096
#define SYSTEM_ERROR_MSG "system error: "

#ifdef __x86_64__
/* code for 64 bit Intel arch */

typedef unsigned long address_t;
#define JB_SP 6
#define JB_PC 7

/* A translation is required when using an address of a variable.
   Use this as a black box in your code. */
address_t translate_address(address_t addr)
{
    address_t ret;
    asm volatile("xor    %%fs:0x30,%0\n"
                 "rol    $0x11,%0\n"
    : "=g" (ret)
    : "0" (addr));
    return ret;
}

#else
/* code for 32 bit Intel arch */

typedef unsigned int address_t;
#define JB_SP 4
#define JB_PC 5

/* A translation is required when using an address of a variable.
   Use this as a black box in your code. */

address_t translate_address(address_t addr)
{
    address_t ret;
    asm volatile("xor    %%gs:0x18,%0\n"
		"rol    $0x9,%0\n"
                 : "=g" (ret)
                 : "0" (addr));
    return ret;
}
#endif

/**
 * @brief this is the class constructor
 * @param f function pointer where the thread will begin his code
 * @param tid the thread it number
 * @param priority the priority that is given to particular thread
 */
Thread::Thread(void (*f)(void), int tid, int priority)
{
    this->__thread_id = tid;
    this->__thread_cs = READY;
    this->f=f;
    this->__thread_priority = priority;

    if (f != nullptr)
    { // not main thread
        address_t sp,pc;
        char * stack_p = new (std::nothrow) char[STACK_SIZE];
        if (stack_p == nullptr)
        {
            std::cerr << SYSTEM_ERROR_MSG << "bad allocation" << std::endl;
            exit(1);
        }
        this->__thread_stack_p = stack_p;
        sp = (address_t) stack_p + STACK_SIZE - sizeof(address_t);
        pc = (address_t) f;
        if (sigsetjmp(env ,1) == 0)
        {
            (env)->__jmpbuf[JB_SP] = translate_address(sp);
            (env)->__jmpbuf[JB_PC] = translate_address(pc);
            sigemptyset(&env->__saved_mask);
        }
    }
    else
    {
        sigsetjmp(env, 1);
    }
}

/**
 * @brief Thread deconstruction
 */
Thread::~Thread()
{
    delete [] this->__thread_stack_p;
}

/**
 * @return thread's ID number
 */
int Thread::getId()
{
    return this->__thread_id;
}

/**
  @return thread's current state RUNNING/READY/BLOCKED
 */
int Thread::getState()
{
    return this->__thread_cs;
}

/**
 * @return the number of thread quantums (how many time this thread has been RUNNING)
 */
int Thread::getQuantum()
{
    return this->__num_of_quantum;
}

/**
 * @return the thread's priority number - 0 highest
 */
int  Thread::getPriority()
{
    return this->__thread_priority;
}

/**
 * @param new_priority changes the thread's priority number - 0 highest
 */
void Thread::setPriority(int new_priority)
{
    this->__thread_priority = new_priority;
}

/**
 * @param next_state changes thread's current state to RUNNING/READY/BLOCKED
 */
void Thread::setState(int next_state)
{
    this->__thread_cs = next_state;
}

/**
 * this function increases the number of thread quantums (how many time this thread has been RUNNING)
 */
void Thread::incrQuantum()
{
    this->__num_of_quantum ++;
}
