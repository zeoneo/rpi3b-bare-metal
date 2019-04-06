#include <stddef.h>
#include <stdint.h>
#include <plibc/stdio.h>

#include <device/keyboard.h>
#include <device/uart0.h>
#include <device/dma.h>
#include <device/usbd.h>
#include <kernel/rpi-armtimer.h>
#include <kernel/rpi-interrupts.h>
#include <kernel/systimer.h>
#include <mem/physmem.h>
#include <mem/virtmem.h>

extern uint32_t __kernel_end;

void kernel_main(uint32_t r0, uint32_t r1, uint32_t atags)
{
	// Declare as unused
	(void)r0;
	(void)r1;
	(void)atags;

	printf("\n-----------------Kernel Started Dude........................\n");
	uart_init();
	// interrupts_init();

	timer_init();
	// mem_init();
	printf("\n Kernel End: 0x%x \n", &__kernel_end);
	// initialize_virtual_memory();
	// uart_puts("\n Hello virtual memory world 123 \n ");

	// show_dma_demo();
	// udelay(4579 * 1000 * 10);
	// printf("\n 64 bit: %lx", 0x1234567812340000);

	UsbInitialise();
	uint32_t address = KeyboardGetAddress(0);
	printf("Keyboard address: %d", address);

	// struct KeyboardLeds leds = {0};
	while (1)
	{
		KeyboardUpdate(address);
		uint8_t key = KeyboardGetChar(address);
		if (key != 0)
		{
			printf("key pressed: > %c <\n", key);
		}
	}
}
