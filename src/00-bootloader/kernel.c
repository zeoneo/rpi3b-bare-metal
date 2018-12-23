#include <stddef.h>
#include <stdint.h>

extern void BRANCHTO(unsigned int);

// Memory-Mapped I/O output
static inline void mmio_write(uint32_t reg, uint32_t data)
{
	*(volatile uint32_t *)reg = data;
}

// Memory-Mapped I/O input
static inline uint32_t mmio_read(uint32_t reg)
{
	return *(volatile uint32_t *)reg;
}

// Loop <delay> times in a way that the compiler won't optimize away
static inline void delay(int32_t count)
{
	asm volatile("__delay_%=: subs %[count], %[count], #1; bne __delay_%=\n"
				 : "=r"(count)
				 : [count] "0"(count)
				 : "cc");
}

enum
{
	// The GPIO registers base address.
	GPIO_BASE = 0x3F200000, // for raspi2 & 3, 0x20200000 for raspi1

	// The offsets for reach register.

	// Controls actuation of pull up/down to ALL GPIO pins.
	GPPUD = (GPIO_BASE + 0x94),

	// Controls actuation of pull up/down for specific GPIO pin.
	GPPUDCLK0 = (GPIO_BASE + 0x98),

	// The base address for UART.
	UART0_BASE = 0x3F201000, // for raspi2 & 3, 0x20201000 for raspi1

	// The offsets for reach register for the UART.
	UART0_DR = (UART0_BASE + 0x00),
	UART0_RSRECR = (UART0_BASE + 0x04),
	UART0_FR = (UART0_BASE + 0x18),
	UART0_ILPR = (UART0_BASE + 0x20),
	UART0_IBRD = (UART0_BASE + 0x24),
	UART0_FBRD = (UART0_BASE + 0x28),
	UART0_LCRH = (UART0_BASE + 0x2C),
	UART0_CR = (UART0_BASE + 0x30),
	UART0_IFLS = (UART0_BASE + 0x34),
	UART0_IMSC = (UART0_BASE + 0x38),
	UART0_RIS = (UART0_BASE + 0x3C),
	UART0_MIS = (UART0_BASE + 0x40),
	UART0_ICR = (UART0_BASE + 0x44),
	UART0_DMACR = (UART0_BASE + 0x48),
	UART0_ITCR = (UART0_BASE + 0x80),
	UART0_ITIP = (UART0_BASE + 0x84),
	UART0_ITOP = (UART0_BASE + 0x88),
	UART0_TDR = (UART0_BASE + 0x8C),
};

void uart_init()
{
	// Disable UART0.
	mmio_write(UART0_CR, 0x00000000);
	// Setup the GPIO pin 14 && 15.

	// Disable pull up/down for all GPIO pins & delay for 150 cycles.
	mmio_write(GPPUD, 0x00000000);
	delay(150);

	// Disable pull up/down for pin 14,15 & delay for 150 cycles.
	mmio_write(GPPUDCLK0, (1 << 14) | (1 << 15));
	delay(150);

	// Write 0 to GPPUDCLK0 to make it take effect.
	mmio_write(GPPUDCLK0, 0x00000000);

	// Clear pending interrupts.
	mmio_write(UART0_ICR, 0x7FF);

	// Set integer & fractional part of baud rate.
	// Divider = UART_CLOCK/(16 * Baud)
	// Fraction part register = (Fractional part * 64) + 0.5
	// UART_CLOCK = 3000000; Baud = 115200.

	// Divider = 3000000 / (16 * 115200) = 1.627 = ~1.
	mmio_write(UART0_IBRD, 1);
	// Fractional part register = (.627 * 64) + 0.5 = 40.6 = ~40.
	mmio_write(UART0_FBRD, 40);

	// Enable FIFO & 8 bit data transmissio (1 stop bit, no parity).
	mmio_write(UART0_LCRH, (1 << 4) | (1 << 5) | (1 << 6));

	// Mask all interrupts.
	mmio_write(UART0_IMSC, (1 << 1) | (1 << 4) | (1 << 5) | (1 << 6) |
							   (1 << 7) | (1 << 8) | (1 << 9) | (1 << 10));

	// Enable UART0, receive & transfer part of UART.
	mmio_write(UART0_CR, (1 << 0) | (1 << 8) | (1 << 9));
}

void uart_putc(unsigned char c)
{
	// Wait for UART to become ready to transmit.
	while (mmio_read(UART0_FR) & (1 << 5))
	{
	}
	mmio_write(UART0_DR, c);
}

unsigned char uart_getc()
{
	// Wait for UART to have received something.
	while (mmio_read(UART0_FR) & (1 << 4))
	{
	}
	return mmio_read(UART0_DR);
}

void uart_puts(const char *str)
{
	for (size_t i = 0; str[i] != '\0'; i++)
		uart_putc((unsigned char)str[i]);
}

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

#if defined(__cplusplus)
extern "C" /* Use C linkage for kernel_main. */
#endif
	void
	kernel_main(uint32_t r0, uint32_t r1, uint32_t atags)
{
	// Declare as unused
	(void)r0;
	(void)r1;
	(void)atags;

	uart_init();

again:
	uart_puts("####################\r\n");
	uart_puts("Inside Bootloader\r\n");
	uart_puts("Boot Atags Addr: ");
	hexstrings(atags);
	uart_putc('\n');
	uart_puts("####################\r\n");

	uart_puts("Requesting kernel\r\n");

	hexstrings(0x1018);
	uart_putc(3);
	uart_putc(3);
	uart_putc(3);

	uint32_t size = uart_getc();
	size |= uart_getc() << 8;
	size |= uart_getc() << 16;
	size |= uart_getc() << 24;

	hexstrings(0x12345678);
	hexstrings(size);

	if (0x8000 + size > 0x2000000)
	{
		uart_puts("SE");
		goto again;
	}
	else
	{
		uart_puts("OK");
	}

	uint8_t *kernel = (uint8_t *)0x8000;
	while (size-- > 0)
	{
		*kernel++ = uart_getc();
	}

	unsigned int addr = 0x8000;

	uart_puts("JUMP");
	BRANCHTO(addr);

	uart_puts("KERNEL FAILED");

	while (1)
		uart_putc(uart_getc());
}