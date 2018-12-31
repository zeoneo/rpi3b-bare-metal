#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include <kernel/uart0.h>
#include <kernel/rpi-armtimer.h>
#include <kernel/rpi-interrupts.h>
#include <kernel/systimer.h>
#include <kernel/physmem.h>
#include <kernel/virtmem.h>

extern void PUT32(unsigned int, unsigned int);
extern unsigned int GET32(unsigned int);
extern void dummy(unsigned int);
extern void enable_irq(void);
extern uint32_t read_cpu_id(void);

extern uint32_t __end;

void init_arm_timer();

void kernel_main(uint32_t r0, uint32_t r1, uint32_t atags)
{
	// Declare as unused
	(void)r0;
	(void)r1;
	(void)atags;

	printf("\n-----------------Kernel Started Dude........................\n");
	uart_init();
	interrupts_init();

	printf("\n Kernel End: 0x%x \n", &__end);
	initialize_virtual_memory();

	uart_puts("\n Hello virtual memory \n ");
	while (1)
	{
	}
}
