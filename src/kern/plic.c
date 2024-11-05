// plic.c - RISC-V PLIC
//

#include "plic.h"
#include "console.h"

#include <stdint.h>

// COMPILE-TIME CONFIGURATION
//

// *** Note to student: you MUST use PLIC_IOBASE for all address calculations,
// as this will be used for testing!

#ifndef PLIC_IOBASE
#define PLIC_IOBASE 0x0C000000
#endif

#define PLIC_SRCCNT 0x400
#define PLIC_CTXCNT 1

#define PENDING 0x1000
#define ENABLE 0x2000
#define PRIORITY 0x200000
#define CLAIM 0x200004

// INTERNAL FUNCTION DECLARATIONS
//

// *** Note to student: the following MUST be declared extern. Do not change these
// function delcarations!

extern void plic_set_source_priority(uint32_t srcno, uint32_t level);
extern int plic_source_pending(uint32_t srcno);
extern void plic_enable_source_for_context(uint32_t ctxno, uint32_t srcno);
extern void plic_disable_source_for_context(uint32_t ctxno, uint32_t srcno);
extern void plic_set_context_threshold(uint32_t ctxno, uint32_t level);
extern uint32_t plic_claim_context_interrupt(uint32_t ctxno);
extern void plic_complete_context_interrupt(uint32_t ctxno, uint32_t srcno);

// Currently supports only single-hart operation. The low-level PLIC functions
// already understand contexts, so we only need to modify the high-level
// functions (plic_init, plic_claim, plic_complete).

// EXPORTED FUNCTION DEFINITIONS
// 

void plic_init(void) {
    int i;

    // Disable all sources by setting priority to 0, enable all sources for
    // context 0 (M mode on hart 0).

    for (i = 0; i < PLIC_SRCCNT; i++) {
        plic_set_source_priority(i, 0);
        plic_enable_source_for_context(0, i);
    }
}

extern void plic_enable_irq(int irqno, int prio) {
    trace("%s(irqno=%d,prio=%d)", __func__, irqno, prio);
    plic_set_source_priority(irqno, prio);
}

extern void plic_disable_irq(int irqno) {
    if (0 < irqno)
        plic_set_source_priority(irqno, 0);
    else
        debug("plic_disable_irq called with irqno = %d", irqno);
}

extern int plic_claim_irq(void) {
    // Hardwired context 0 (M mode on hart 0)
    trace("%s()", __func__);
    return plic_claim_context_interrupt(0);
}

extern void plic_close_irq(int irqno) {
    // Hardwired context 0 (M mode on hart 0)
    trace("%s(irqno=%d)", __func__, irqno);
    plic_complete_context_interrupt(0, irqno);
}

// INTERNAL FUNCTION DEFINITIONS
//

void plic_set_source_priority(uint32_t srcno, uint32_t level) {
    //Sets the priority level fo a pecific interrupt source.
     uint32_t* address = (uint32_t*)PLIC_IOBASE + srcno;  // this gives the address of the priority of this source
    *address = level;
    // FIXME your code goes here
}

int plic_source_pending(uint32_t srcno) {
    //Checks if an interrupt source is pending by inspecting the pending array.
    uint32_t offset = srcno/32; // offset calc
    uint32_t bit = 1<<(srcno%32);  //bit calc
    uint32_t *address = (uint32_t*)(PLIC_IOBASE+ PENDING)+offset;  // address driviation
    return (*address >> bit) & 1;  //return
    // FIXME your code goes here
}

void plic_enable_source_for_context(uint32_t ctxno, uint32_t srcno) {
    //Enables a specific interrupt source for a given context.
    uint32_t bit = 1<<(srcno%32); // set bit 
    uint32_t *address = (uint32_t*)(ENABLE+ PLIC_IOBASE)+0X80*ctxno/4+srcno/32;  //access the address
    *address |= bit;  //set the bit 

    // FIXME your code goes here
}

void plic_disable_source_for_context(uint32_t ctxno, uint32_t srcid) {
    //Disables a specific interrupt source for a given context
    uint32_t bit = ~1<<(srcid%32); // set bit
    uint32_t *address = (uint32_t*)(ENABLE+ PLIC_IOBASE)+0X80*ctxno/4+srcid/32; // access address
    *address &= bit;  // set bit 
}

void plic_set_context_threshold(uint32_t ctxno, uint32_t level) {
    //Sets the interrupt priority threshold for a specific context
    volatile uint32_t* address = (uint32_t*)(PLIC_IOBASE + PRIORITY) + 0x1000 * ctxno / 4; // access address
    *address = level; // set threshold
}

uint32_t plic_claim_context_interrupt(uint32_t ctxno) {
    // Claims an interrupt for a give context
    uint32_t *address = (uint32_t*)(CLAIM+ PLIC_IOBASE)+0x1000*ctxno/4; // access the address
    return *address; // return the content
}
void plic_complete_context_interrupt(uint32_t ctxno, uint32_t srcno) {
    //Completes the handling of an interrupt for a given context
    uint32_t *address = (uint32_t*)(CLAIM+ PLIC_IOBASE)+0x1000*ctxno/4;// access the address
    *address = srcno; // writes in the source
    // FIXME your code goes here
}