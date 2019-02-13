
#include <stdint.h>
#include "rpi-systimer.h"
#include "uart0.h"
static rpi_sys_timer_t *rpiSystemTimer = (rpi_sys_timer_t *)RPI_SYSTIMER_BASE;

rpi_sys_timer_t *RPI_GetSystemTimer(void)
{
    return rpiSystemTimer;
}

void RPI_WaitMicroSeconds(uint32_t us)
{
    volatile uint32_t ts = rpiSystemTimer->counter_lo;

    while ((rpiSystemTimer->counter_lo - ts) < us)
    {
        uart_putc('\n');
        hexstrings(rpiSystemTimer->counter_lo);
        /* BLANK */
    }
    uart_puts("leaving.\n");
    hexstrings(rpiSystemTimer->counter_lo);
}
