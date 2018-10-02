#include <stddef.h>
#include <stdint.h>

#include "uart0.h"
#include "rpi-armtimer.h"
#include "rpi-systimer.h"
#include "rpi-interrupts.h"

#if defined(__cplusplus)
extern "C" /* Use C linkage for kernel_main. */
#endif

void kernel_main(uint32_t r0, uint32_t r1, uint32_t atags)
{
	// Declare as unused
	(void) r0;
	(void) r1;
	(void) atags;
 
	uart_init();
	
	uart_puts("Hello, kernel World!\r\n");
	uart_puts("Hello, kernel World!\r\n");
	uart_puts("Hello, kernel World!\r\n");
	uart_puts("Hello, kernel World!\r\n");


    // RPI_GetIrqController()->Enable_Basic_IRQs = RPI_BASIC_ARM_TIMER_IRQ;
	uart_puts("Enabled basic Timer IRQ \r\n"); 

    // RPI_GetArmTimer()->Load = 0x400;
    // RPI_GetArmTimer()->Control =
    //         RPI_ARMTIMER_CTRL_23BIT |
    //         RPI_ARMTIMER_CTRL_ENABLE |
    //         RPI_ARMTIMER_CTRL_INT_ENABLE |
    //         RPI_ARMTIMER_CTRL_PRESCALE_256;

	uart_puts("Enabling CPU Interrupts \r\n"); 
	/* Defined in boot.S */
    // _enable_interrupts();
	uart_puts("Enabled CPU Interrupts \r\n"); 

	uart_puts("Terminal Mode: ON \r\n"); 
	while (1)
		uart_putc(uart_getc());
}