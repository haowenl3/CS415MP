// timer.h
// 

#ifndef _TIMER_H_
#define _TIMER_H_

#include <stdint.h>
#include "thread.h" // for struct condition

// EXPORTED GLOBAL VARIABLE DECLARATIONS
// 

extern char timer_initialized;

extern struct condition tick_1Hz;
extern struct condition tick_10Hz;

extern uint64_t tick_1Hz_count;
extern uint64_t tick_10Hz_count;

// EXPORTED FUNCTION DECLARATIONS
//

extern void timer_init(void);
extern void timer_start(void);

extern void timer_intr_handler(void); // called from intr.c

#endif // _TIMER_H_