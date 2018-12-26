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

	hexstring(0xDEADBEEF);

	PUT32(0x00045678, 0x00045678);
	PUT32(0x00145678, 0x00145678);
	PUT32(0x00245678, 0x00245678);
	PUT32(0x00345678, 0x00345678);

	PUT32(0x00346678, 0x00346678);
	PUT32(0x00146678, 0x00146678);

	PUT32(0x0AA45678, 0x12345678);
	PUT32(0x0BB45678, 0x12345678);
	PUT32(0x0CC45678, 0x12345678);
	PUT32(0x0DD45678, 0x12345678);

	hexstring(GET32(0x00045678));
	hexstring(GET32(0x00145678));
	hexstring(GET32(0x00245678));
	hexstring(GET32(0x00345678));
	uart_putc(0x0D);
	uart_putc(0x0A);

	for (ra = 0;; ra += 0x00100000)
	{
		mmu_section(ra, ra, 0x0000);
		if (ra == 0xFFF00000)
			break;
	}

	//mmu_section(0x00000000,0x00000000,0x0000|8|4);
	//mmu_section(0x00100000,0x00100000,0x0000);
	//mmu_section(0x00200000,0x00200000,0x0000);
	//mmu_section(0x00300000,0x00300000,0x0000);
	//peripherals
	mmu_section(0x30000000, 0x30000000, 0x0000); //NOT CACHED!
	mmu_section(0x30200000, 0x30200000, 0x0000); //NOT CACHED!

	start_mmu(MMUTABLEBASE, 0x00000001 | 0x1000 | 0x0004); //[23]=0 subpages enabled = legacy ARMv4,v5 and v6

	hexstring(GET32(0x00045678));
	hexstring(GET32(0x00145678));
	hexstring(GET32(0x00245678));
	hexstring(GET32(0x00345678));
	uart_putc(0x0D);
	uart_putc(0x0A);

	mmu_section(0x00100000, 0x00300000, 0x0000);
	mmu_section(0x00200000, 0x00000000, 0x0000);
	mmu_section(0x00300000, 0x00100000, 0x0000);
	invalidate_tlbs();

	hexstring(GET32(0x00045678));
	hexstring(GET32(0x00145678));
	hexstring(GET32(0x00245678));
	hexstring(GET32(0x00345678));
	uart_putc(0x0D);
	uart_putc(0x0A);

	mmu_small(0x0AA45000, 0x00145000, 0, 0x00000400);
	mmu_small(0x0BB45000, 0x00245000, 0, 0x00000800);
	mmu_small(0x0CC45000, 0x00345000, 0, 0x00000C00);
	mmu_small(0x0DD45000, 0x00345000, 0, 0x00001000);
	mmu_small(0x0DD46000, 0x00146000, 0, 0x00001000);
	//put these back
	mmu_section(0x00100000, 0x00100000, 0x0000);
	mmu_section(0x00200000, 0x00200000, 0x0000);
	mmu_section(0x00300000, 0x00300000, 0x0000);
	invalidate_tlbs();

	hexstring(GET32(0x0AA45678));
	hexstring(GET32(0x0BB45678));
	hexstring(GET32(0x0CC45678));
	uart_putc(0x0D);
	uart_putc(0x0A);

	hexstring(GET32(0x00345678));
	hexstring(GET32(0x00346678));
	hexstring(GET32(0x0DD45678));
	hexstring(GET32(0x0DD46678));
	uart_putc(0x0D);
	uart_putc(0x0A);

	//access violation.

	mmu_section(0x00100000, 0x00100000, 0x0020);
	invalidate_tlbs();

	hexstring(GET32(0x00045678));
	hexstring(GET32(0x00145678));
	hexstring(GET32(0x00245678));
	hexstring(GET32(0x00345678));
	uart_putc(0x0D);
	uart_putc(0x0A);

	hexstring(0xDEADBEEF);

	while (1)
	{
	}
}
