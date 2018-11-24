#include <stddef.h>
#include <stdint.h>

#include "uart0.h"
#include "rpi-armtimer.h"
#include "rpi-systimer.h"
#include "rpi-interrupts.h"


extern void PUT32 ( unsigned int, unsigned int );
extern unsigned int GET32 ( unsigned int );
extern void dummy ( unsigned int );
extern void enable_irq ( void );


#define GPFSEL2 0x3F200008
#define GPSET0  0x3F20001C
#define GPCLR0  0x3F200028

#define ARM_TIMER_LOD 0x3F00B400
#define ARM_TIMER_VAL 0x3F00B404
#define ARM_TIMER_CTL 0x3F00B408
#define ARM_TIMER_CLI 0x3F00B40C
#define ARM_TIMER_RIS 0x3F00B410
#define ARM_TIMER_MIS 0x3F00B414
#define ARM_TIMER_RLD 0x3F00B418
#define ARM_TIMER_DIV 0x3F00B41C
#define ARM_TIMER_CNT 0x3F00B420

#define SYSTIMERCLO 0x3F003004
#define GPFSEL1 0x3F200004
#define GPSET0  0x3F20001C
#define GPCLR0  0x3F200028
#define GPFSEL3 0x3F20000C
#define GPFSEL4 0x3F200010
#define GPSET1  0x3F200020
#define GPCLR1  0x3F20002C

#define IRQ_BASIC 0x3F00B200
#define IRQ_PEND1 0x3F00B204
#define IRQ_PEND2 0x3F00B208
#define IRQ_FIQ_CONTROL 0x3F00B210
#define IRQ_ENABLE_BASIC 0x3F00B218
#define IRQ_DISABLE_BASIC 0x3F00B224

volatile unsigned int icount;

void c_irq_handler ( void )
{
    icount++;
	uart_puts("In interrupt handler !\r\n");
    RPI_GetArmTimer()->IRQClear = 1;
}

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


    RPI_GetIrqController()->Enable_Basic_IRQs = RPI_BASIC_ARM_TIMER_IRQ;
	uart_puts("Enabled basic Timer IRQ \r\n"); 

    RPI_GetArmTimer()->Load = 0x400;
    RPI_GetArmTimer()->Control =
            RPI_ARMTIMER_CTRL_23BIT |
            RPI_ARMTIMER_CTRL_ENABLE |
            RPI_ARMTIMER_CTRL_INT_ENABLE |
            RPI_ARMTIMER_CTRL_PRESCALE_256;

	uart_puts("Enabling CPU Interrupts \r\n"); 
	/* Defined in boot.S */
    _enable_interrupts();
	uart_puts("Enabled CPU Interrupts \r\n"); 
	while (1);

}