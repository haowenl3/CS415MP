//           uart.c - NS16550a-based uart port
//           

#include "uart.h"

#include <stdint.h>

#include "device.h"
#include "error.h"
#include "thread.h"
#include "heap.h"
#include "halt.h"
#include "intr.h"
#include <limits.h>     //the compiler suggest me to do so

//           COMPILE-TIME CONSTANT DEFINITIONS
//          

#ifndef UART0_IOBASE
#define UART0_IOBASE 0x10000000
#endif

#ifndef SERIAL_RBUFSZ
#define SERIAL_RBUFSZ 64
#endif

#ifndef UART_IRQ_PRIO
#define UART_IRQ_PRIO 3
#endif

//           INTERNAL TYPE DEFINITIONS
//           

struct uart_regs {
	union {
		//           DLAB=0 read
		char rbr;
		//           DLAB=0 write
		char thr;
		//           DLAB=1
		uint8_t dll;
	};
	
	union {
		//           DLAB=0
		uint8_t ier;
		//           DLAB=1
		uint8_t dlm;
	};
	
	union {
		//           read
		uint8_t iir;
		//           write
		uint8_t fcr;
	};

	uint8_t lcr;
	uint8_t mcr;
	uint8_t lsr;
	uint8_t msr;
	uint8_t scr;
};

#define LCR_DLAB (1 << 7)
#define LSR_OE (1 << 1)
#define LSR_DR (1 << 0)
#define LSR_THRE (1 << 5)
#define IER_DREIE (1 << 0)
#define IER_THREIE (1 << 1)

struct ringbuf {
    //           head of queue (from where elements are removed)
    uint16_t hpos;
    //           tail of queue (where elements are inserted)
    uint16_t tpos;
    char data[SERIAL_RBUFSZ];
};

struct uart_device {
	volatile struct uart_regs * regs;
	int irqno;

	//           number of times OE was set
	uint32_t rxovrcnt;

	int8_t opened;

	struct io_intf io_intf;
	
	struct condition rxbnotempty;
	struct condition txbnotfull;	

	struct ringbuf rxbuf;
	struct ringbuf txbuf;
};

//           INTERNAL FUNCTION DEFINITIONS
//          

static int uart_open(struct io_intf ** ioptr, void * aux);
static void uart_close(struct io_intf * io);
static long uart_read(struct io_intf * io, void * buf, unsigned long bufsz);
static long uart_write(struct io_intf * io, const void * buf, unsigned long n);

static void uart_isr(int irqno, void * driver_private);

static int uart_open_ebusy(struct io_intf ** ioptr, void * aux);

static void rbuf_init(struct ringbuf * rbuf);
static int rbuf_empty(const struct ringbuf * rbuf);
static int rbuf_full(const struct ringbuf * rbuf);
static void rbuf_put(struct ringbuf * rbuf, char c);
static char rbuf_get(struct ringbuf * rbuf);

//           EXPORTED FUNCTION DEFINITIONS
//           

void uart_attach(void * mmio_base, int irqno) {
	static const struct io_ops uart_ops = {
		.close = uart_close,
		.read = uart_read,
		.write = uart_write
	};

	struct uart_device * dev;

	//           UART0 is used for the console, so can't be opened

	if (mmio_base == (void*)UART0_IOBASE) {
		device_register("ser", &uart_open_ebusy, NULL);
		return;
	}

	dev = kcalloc(1, sizeof(struct uart_device));

	dev->regs = mmio_base;
	dev->irqno = irqno;
	dev->io_intf.ops = &uart_ops;

	condition_init(&dev->rxbnotempty, "rxnotempty");
	condition_init(&dev->txbnotfull, "txnotfull");

	rbuf_init(&dev->rxbuf);
	rbuf_init(&dev->txbuf);

	dev->regs->ier = 0;
    dev->regs->lcr = LCR_DLAB;
    //           fence o,o ?
    dev->regs->dll = 0x01;
    dev->regs->dlm = 0x00;
    //           fence o,o ?
    //           DLAB=0
    dev->regs->lcr = 0;

	intr_register_isr(irqno, UART_IRQ_PRIO, uart_isr, dev);
	device_register("ser", &uart_open, dev);
}
	
int uart_open(struct io_intf ** ioptr, void * aux) {
	struct uart_device * const dev = aux;

	assert (ioptr != NULL);

	if (dev->opened)
		return -EBUSY;
	
	//           Reset receive and transmit buffers
	
	rbuf_init(&dev->rxbuf);
	rbuf_init(&dev->txbuf);

	//           Read receive buffer register to clear it. Enable RX interrupts only.

	//           forces a read
	dev->regs->rbr;
	dev->regs->ier = IER_DREIE;

	intr_enable_irq(dev->irqno);

	*ioptr = &dev->io_intf;
	dev->opened = 1;

	return 0;
}

void uart_close(struct io_intf * io) {
	struct uart_device * const dev =
		(void*)io - offsetof(struct uart_device, io_intf);

	trace("%s()", __func__);
	assert (io != NULL);
	assert(dev->opened);
	
	//           Disable all interrupts from device

	dev->regs->ier = 0;
	intr_disable_irq(dev->irqno);
	
	dev->opened = 0;
}

long uart_read(struct io_intf * io, void * buf, unsigned long bufsz) {
	struct uart_device * const dev =
		(void*)io - offsetof(struct uart_device, io_intf);
	//           position in buf to put next byte
	char * p = buf;

	trace("%s(buf=%p,bufsz=%ld)", __func__, buf, bufsz);
	assert (io != NULL);
	assert (dev->opened);

	//           We can only report reads of up to LONG_MAX bytes

	if (LONG_MAX < bufsz)
		bufsz = LONG_MAX;
	
	if (bufsz == 0)
		return 0;

	//           Wait until ring buffer contains some data. An optimization here might
	//           be to try to read some data without disabling interrupts first, and
	//           only disabling them and waiting on the condition if we need to.
	//           
	//           Check your understanding: why do we need to disable interrupts? Can we
	//           call condition_wait(&dev->rxavail) without disabling interrupts?
	//           
	//           Could we implement this as a busy-wait?
	//           

	intr_disable();

	while (rbuf_empty(&dev->rxbuf))
		condition_wait(&dev->rxbnotempty);

	intr_enable();

	while (!rbuf_empty(&dev->rxbuf) && p - (char*)buf < bufsz)
		*p++ = rbuf_get(&dev->rxbuf);
	
	//           enable receive interrupts
	dev->regs->ier |= IER_DREIE;
	
	return p - (char*)buf;
}

long uart_write(struct io_intf * io, const void * buf, unsigned long n) {
	struct uart_device * const dev =
		(void*)io - offsetof(struct uart_device, io_intf);
	//           position in buf to get next byte
	const char * p = buf;
	
	trace("%s(n=%ld)", __func__, n);
	assert (io != NULL);
	assert (dev->opened);

	//          We can only report writes of up to LONG_MAX size

	if (LONG_MAX < n)
		n = LONG_MAX;

	//           Wait until there is room in the transmit ring buffer.

	while (p - (char*)buf < n) {
		intr_disable();
		while (rbuf_full(&dev->txbuf))
			condition_wait(&dev->txbnotfull);
		intr_enable();

		while (!rbuf_full(&dev->txbuf) && p - (char*)buf < n)
			rbuf_put(&dev->txbuf, *p++);
		
		dev->regs->ier |= IER_THREIE;
	}

	return p - (char*)buf;
}

void uart_isr(int irqno, void * aux) {
	struct uart_device * const dev = aux;
	const uint_fast8_t line_status = dev->regs->lsr;

	if (line_status & LSR_OE)
		dev->rxovrcnt += 1;
	
	if (line_status & LSR_DR) {
		if (!rbuf_full(&dev->rxbuf)) {
			if (rbuf_empty(&dev->rxbuf))
				condition_broadcast(&dev->rxbnotempty);
			rbuf_put(&dev->rxbuf, dev->regs->rbr);
		} else
			dev->regs->ier &= ~IER_DREIE;
	}

	if (line_status & LSR_THRE) {
		if (!rbuf_empty(&dev->txbuf)) {
			if (rbuf_full(&dev->txbuf))
				condition_broadcast(&dev->txbnotfull);
			dev->regs->thr = rbuf_get(&dev->txbuf);
		} else
			dev->regs->ier &= ~IER_THREIE;
	}
}

int uart_open_ebusy (
	struct io_intf ** __attribute__ ((unused)) ioptr,
	void * __attribute__ ((unused)) aux)
{
	return -EBUSY;
}

void rbuf_init(struct ringbuf * rbuf) {
    rbuf->hpos = 0;
    rbuf->tpos = 0;
}

int rbuf_empty(const struct ringbuf * rbuf) {
    return (rbuf->hpos == rbuf->tpos);
}

int rbuf_full(const struct ringbuf * rbuf) {
    return ((uint16_t)(rbuf->tpos - rbuf->hpos) == SERIAL_RBUFSZ);
}

void rbuf_put(struct ringbuf * rbuf, char c) {
    uint_fast16_t tpos;

    tpos = rbuf->tpos;
    rbuf->data[tpos % SERIAL_RBUFSZ] = c;
    asm volatile ("" ::: "memory");
    rbuf->tpos = tpos + 1;
}

char rbuf_get(struct ringbuf * rbuf) {
    uint_fast16_t hpos;
    char c;

    hpos = rbuf->hpos;
    c = rbuf->data[hpos % SERIAL_RBUFSZ];
    asm volatile ("" ::: "memory");
    rbuf->hpos = hpos + 1;
    return c;
}

//           The functions below provide polled uart input and output. They are used
//           by the console functions to produce output from pritnf().

#define UART0 (*(volatile struct uart_regs*)UART0_IOBASE)

void com0_init(void) {
	UART0.ier = 0x00;

	//           Configure UART0. We set the baud rate divisor to 1, the lowest value,
	//           for the fastest baud rate. In a physical system, the actual baud rate
	//           depends on the attached oscillator frequency. In a virtualized system,
	//           it doesn't matter.
	
	UART0.lcr = LCR_DLAB;
	UART0.dll = 0x01;
	UART0.dlm = 0x00;

	//           The com0_putc and com0_getc functions assume DLAB=0.

	UART0.lcr = 0;
}

void com0_putc(char c) {
	//           Spin until THR is empty
	while (!(UART0.lsr & LSR_THRE))
		continue;

	UART0.thr = c;
}

char com0_getc(void) {
	//           Spin until RBR contains a byte
	while (!(UART0.lsr & LSR_DR))
		continue;
	
	return UART0.rbr;
}