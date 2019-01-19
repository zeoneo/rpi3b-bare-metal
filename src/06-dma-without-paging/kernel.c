#include <stddef.h>
#include <stdint.h>

#include "uart0.h"
#include "rpi-interrupts.h"
#include "virtmem.h"
#include "dma.h"

extern void PUT32(unsigned int, unsigned int);
extern unsigned int GET32(unsigned int);
extern void dummy(unsigned int);
extern void enable_irq(void);

#define IRQ_BASIC 0x3F00B200
#define IRQ_PEND1 0x3F00B204
#define IRQ_PEND2 0x3F00B208
#define IRQ_FIQ_CONTROL 0x3F00B210
#define IRQ_ENABLE_BASIC 0x3F00B218
#define IRQ_DISABLE_BASIC 0x3F00B224

void kernel_main(uint32_t r0, uint32_t r1, uint32_t atags)
{
	// Declare as unused
	(void)r0;
	(void)r1;
	(void)atags;

	uart_init();

	uart_puts("Hello, kernel World!\r\n");
	uart_puts("Hello, kernel World!\r\n");
	uart_puts("Hello, kernel World!\r\n");
	uart_puts("Hello, kernel World!\r\n");

	RPI_GetIrqController()->Enable_Basic_IRQs = RPI_BASIC_ARM_TIMER_IRQ;
	uart_puts("Enabled basic Timer IRQ \r\n");

	uart_puts("Enabling CPU Interrupts \r\n");
	/* Defined in boot.S */
	_enable_interrupts();
	uart_puts("Enabled CPU Interrupts \r\n");
	// initialize_virtual_memory();
	// uart_puts("After virtual memory.... HELLO WORLD..! \r\n");

	show_dma_demo();
	while (1)
		;
}