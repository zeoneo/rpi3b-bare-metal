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
	// arm_timer_init();
	timer_init();
	mem_init();

	printf("Arm Memory :%ld MB \n", get_mem_size() / (1024 * 1024));
	printf("Number of Free Pages: %d \n", get_num_of_free_pages());

	int *new_page_base = (int *)alloc_page();
	printf("Number of Free Pages: %d \n", get_num_of_free_pages());
	free_page(new_page_base);
	printf("Number of Free Pages: %d \n", get_num_of_free_pages());

	unsigned int ra;

	printf("\nCpu Id :0x%x\n", read_cpu_id());
	initialize_virtual_memory();

	uart_puts("\n after virtual memory 1234 \n ");
	printf("Hellow World agian.");
	while (1)
	{
	}
}
