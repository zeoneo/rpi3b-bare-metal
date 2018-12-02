#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include <kernel/uart0.h>
#include <kernel/rpi-armtimer.h>
#include <kernel/rpi-interrupts.h>
#include <kernel/rpi-mailbox.h>
#include <kernel/rpi-mailbox-interface.h>
#include <kernel/systimer.h>

extern void PUT32(unsigned int, unsigned int);
extern unsigned int GET32(unsigned int);
extern void dummy(unsigned int);
extern void enable_irq(void);

#define GPFSEL2 0x3F200008
#define GPSET0 0x3F20001C
#define GPCLR0 0x3F200028

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
#define GPSET0 0x3F20001C
#define GPCLR0 0x3F200028
#define GPFSEL3 0x3F20000C
#define GPFSEL4 0x3F200010
#define GPSET1 0x3F200020
#define GPCLR1 0x3F20002C

#define IRQ_BASIC 0x3F00B200
#define IRQ_PEND1 0x3F00B204
#define IRQ_PEND2 0x3F00B208
#define IRQ_FIQ_CONTROL 0x3F00B210
#define IRQ_ENABLE_BASIC 0x3F00B218
#define IRQ_DISABLE_BASIC 0x3F00B224

void hexstrings(unsigned int d)
{
	//unsigned int ra;
	unsigned int rb;
	unsigned int rc;

	rb = 32;
	while (1)
	{
		rb -= 4;
		rc = (d >> rb) & 0xF;
		if (rc > 9)
			rc += 0x37;
		else
			rc += 0x30;
		uart_putc(rc);
		if (rb == 0)
			break;
	}
	uart_putc(0x20);
}

void kernel_main(uint32_t r0, uint32_t r1, uint32_t atags)
{
	// Declare as unused
	(void)r0;
	(void)r1;
	(void)atags;

	uart_init();

	printf("Hello, kernel World!\n");
	uart_puts("Hello, kernel World!\n");
	uart_puts("Hello, kernel World!\n");
	uart_puts("Hello, kernel World!\n");

	RPI_GetIrqController()->Enable_Basic_IRQs = RPI_BASIC_ARM_TIMER_IRQ;
	printf("Enabled basic Timer IRQ :%d  %f  %s \n", 123132213, 345.345345, "Some string");

	RPI_GetArmTimer()->Load = 0x400;
	RPI_GetArmTimer()->Control =
		RPI_ARMTIMER_CTRL_23BIT |
		RPI_ARMTIMER_CTRL_ENABLE |
		RPI_ARMTIMER_CTRL_INT_ENABLE |
		RPI_ARMTIMER_CTRL_PRESCALE_256;

	uart_puts("Enabling CPU Interrupts \r\n");
	/* Defined in boot.S */

	RPI_PropertyInit();
	RPI_PropertyAddTag(TAG_GET_BOARD_MODEL);
	RPI_PropertyAddTag(TAG_GET_BOARD_REVISION);
	RPI_PropertyAddTag(TAG_GET_FIRMWARE_VERSION);
	RPI_PropertyAddTag(TAG_GET_BOARD_MAC_ADDRESS);
	RPI_PropertyAddTag(TAG_GET_BOARD_SERIAL);
	RPI_PropertyAddTag(TAG_GET_MAX_CLOCK_RATE, TAG_CLOCK_ARM);
	RPI_PropertyProcess();

	rpi_mailbox_property_t *mp;
	mp = RPI_PropertyGet(TAG_GET_FIRMWARE_VERSION);

	uart_puts("MAX CLOCK Version: ");

	if (mp)
	{
		uart_puts(" CLOCK NOW: ");
		hexstrings(mp->data.value_32);
		printf("Clokc: %d", (int)(mp->data.value_32));
	}
	else
		uart_puts(" NULL\r\n");

	interrupts_init();
	timer_init();

	uart_puts("Enabled CPU Interrupts \n");
	udelay(3000000);
	uart_puts("Enabled CPU Interrupts \n");
	timer_set(3000000);
	timer_set(10000000);
	timer_set(10000000);
	timer_set(10000000);

	while (1)
		;
}