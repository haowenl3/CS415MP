// plic.h - RISC-V PLIC
//

#ifndef _PLIC_H_
#define _PLIC_H_

#include <limits.h>

#define PLIC_PRIO_MIN 1
#define PLIC_PRIO_MAX 7

extern void plic_init(void);

extern void plic_enable_irq(int irqno, int prio);
extern void plic_disable_irq(int irqno);

extern int plic_claim_irq(void);
extern void plic_close_irq(int irqno);

#endif