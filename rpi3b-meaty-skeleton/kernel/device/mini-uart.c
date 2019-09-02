#include <stddef.h>
#include <stdint.h>
#include <device/mini-uart.h>
#include <device/gpio.h>

extern void dummy ( unsigned int );

#define PBASE  0x3F000000

#define GPFSEL1         (PBASE+0x00200004)
#define GPSET0          (PBASE+0x0020001C)
#define GPCLR0          (PBASE+0x00200028)
#define GPPUD           (PBASE+0x00200094)
#define GPPUDCLK0       (PBASE+0x00200098)

#define AUX_ENABLES     (PBASE+0x00215004)
#define AUX_MU_IO_REG   (PBASE+0x00215040)
#define AUX_MU_IER_REG  (PBASE+0x00215044)
#define AUX_MU_IIR_REG  (PBASE+0x00215048)
#define AUX_MU_LCR_REG  (PBASE+0x0021504C)
#define AUX_MU_MCR_REG  (PBASE+0x00215050)
#define AUX_MU_LSR_REG  (PBASE+0x00215054)
#define AUX_MU_MSR_REG  (PBASE+0x00215058)
#define AUX_MU_SCRATCH  (PBASE+0x0021505C)
#define AUX_MU_CNTL_REG (PBASE+0x00215060)
#define AUX_MU_STAT_REG (PBASE+0x00215064)
#define AUX_MU_BAUD_REG (PBASE+0x00215068)

static inline void mmio_write(uint32_t reg, uint32_t data)
{
	*(volatile uint32_t *)reg = data;
}

// Memory-Mapped I/O input
static inline uint32_t mmio_read(uint32_t reg)
{
	return *(volatile uint32_t *)reg;
}

unsigned int mini_uart_getc ()
{
    while(1)
    {
        if(mmio_read(AUX_MU_LSR_REG)&0x01) break;
    }
    return(mmio_read(AUX_MU_IO_REG)&0xFF);
}

//------------------------------------------------------------------------
void mini_uart_putc ( uint32_t c )
{
    while(1)
    {
        if(mmio_read(AUX_MU_LSR_REG)&0x20) break;
    }
    mmio_write(AUX_MU_IO_REG,c);
}

void mini_uart_puts(const char *str)
{
	for (size_t i = 0; str[i] != '\0'; i++)
		mini_uart_putc((unsigned char)str[i]);
}

//------------------------------------------------------------------------
void mini_hexstrings(uint32_t d)
{
    //unsigned int ra;
    unsigned int rb;
    unsigned int rc;

    rb=32;
    while(1)
    {
        rb-=4;
        rc=(d>>rb)&0xF;
        if(rc>9) rc+=0x37; else rc+=0x30;
        mini_uart_putc(rc);
        if(rb==0) break;
    }
    mini_uart_putc(0x20);
}

//------------------------------------------------------------------------
void mini_uart_init ( void )
{
    

    mmio_write(AUX_ENABLES,1);
    mmio_write(AUX_MU_IER_REG,0);
    mmio_write(AUX_MU_CNTL_REG,0);
    mmio_write(AUX_MU_LCR_REG,3);
    mmio_write(AUX_MU_MCR_REG,0);
    mmio_write(AUX_MU_IER_REG,0);
    mmio_write(AUX_MU_IIR_REG,0xC6);
    mmio_write(AUX_MU_BAUD_REG,270);
    // unsigned int ra=mmio_read(GPFSEL1);
    // ra&=~(7<<12); //gpio14
    // ra|=2<<12;    //alt5
    // ra&=~(7<<15); //gpio15
    // ra|=2<<15;    //alt5
    // mmio_write(GPFSEL1,ra);
    // mmio_write(GPPUD,0);
    select_alt_func(14, Alt5);
	select_alt_func(15, Alt5);
    disable_pulling(14);
    disable_pulling(15);

    mmio_write(AUX_MU_CNTL_REG,3);
}