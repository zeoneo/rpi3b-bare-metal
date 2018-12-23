#include <kernel/systimer.h>
#include <kernel/rpi-interrupts.h>
#include <kernel/uart0.h>

static timer_registers_t *timer_regs;

static void timer_irq_handler(void)
{
    uart_puts("\n *** timer irq handler called");
    timer_set(34500000);
}

static void timer_irq_clearer(void)
{
    timer_regs->control.timer1_matched = 1;
    uart_puts("\n *** timer irq clearer called");
}

void timer_init(void)
{
    timer_regs = (timer_registers_t *)SYSTEM_TIMER_BASE;
    register_irq_handler(RPI_BASIC_ARM_TIMER_IRQ, timer_irq_handler, timer_irq_clearer);
}

void timer_set(uint32_t usecs)
{
    timer_regs->timer1 = timer_regs->counter_low + usecs;
}

__attribute__((optimize(0))) void udelay(uint32_t usecs)
{
    volatile uint32_t curr = timer_regs->counter_low;
    volatile uint32_t x = timer_regs->counter_low - curr;
    while (x < usecs)
    {
        x = timer_regs->counter_low - curr;
    }
}
