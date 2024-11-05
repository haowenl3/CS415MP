// thread.h - A thread of execution
//

#ifndef _THREAD_H_
#define _THREAD_H_

#include "trap.h"
#include <stddef.h>

struct thread; // forward decl.

struct thread_list {
    struct thread * head;
    struct thread * tail;
};

struct condition {
    const char * name;
	struct thread_list wait_list;
};

// EXPORTED GLOBAL VARIABLES
// 

extern char thread_initialized; // initially 0, set to 1 by thread_init

// EXPORTED FUNCTION DECLARATIONS
//

extern void thread_init(void);

// int running_thread(void)
// Returns the thread id of the currently running thread.

int running_thread(void);

// int thread_spawn(const char * name, void (*start)(void *), void * arg)
// Creates and starts a new thread. Argument /name/ is the name of the thread
// (optional, may be NULL), /start/ is the thread entry point, and /arg/ is an
// argument passed to the thread. The thread is added to the runnable thread
// list. It is safe for /start/ to return, which is equivalent to calling
// thread_exit from /start/.
// Returns the thread id of the spawned thread or a negative value on error.

extern int thread_spawn(const char * name, void (*start)(void *), void * arg);

// void thread_yield(void)
// Yields the CPU to another thread and returns when the current thread is next
// scheduled to run.

extern void thread_yield(void);

// int thread_join_any(void) int thread_join(int tid) Waits for a child thread
// of the current thread to exit. The thread_join_any function waits for any of
// the current thread's children to exit, while thread_join waits for a specific
// thread, given by /tid/, to exit.

extern int thread_join_any(void);
extern int thread_join(int tid);

// void thread_exit(void)
// Terminates the currently running thread and does not return.

extern void thread_exit(void) __attribute__ ((noreturn));

// void condition_init(struct condition * cond, const char * name)
// Initializes a condition variable. Argument /cond/ is a pointer to a struct
// condition to initialize. Argument /name/ is the name of the thread, which may
// be NULL. It is valid initialize a struct condition with all zeroes.

extern void condition_init(struct condition * cond, const char * name);

// void condition_wait(struct condition * cond)
// Suspends the current thread until a condition is signalled by another thread
// or interrupt service routine. The condition_wait function may be called with
// interrupts disabled. It will enable interrupts while the thread is suspended
// and will restore interrupt enable/disable state to its value when called.
// Note that in cases where a thread needs to wait for a condition to be
// signalled by an ISR, condition_wait() should be called with interrupts
// disabled to avoid a race condition.

extern void condition_wait(struct condition * cond);

// void condition_broadcast(struct condition * cond)
// Marks any threads waiting on the condition runnable. condition_broadcast may
// be called from an ISR. Calling condition_broadcast() does not affect the
// currently running thread.

extern void condition_broadcast(struct condition * cond);

#endif // _THREAD_H_