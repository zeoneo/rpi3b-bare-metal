#ifndef RPI_INTERRUPTS_H
#define RPI_INTERRUPTS_H

#include <stdint.h>
#include <kernel/uart0.h>
#include "rpi-base.h"

#define RPI_INTERRUPT_CONTROLLER_BASE (PERIPHERAL_BASE + 0xB200)

typedef void (*interrupt_handler_f)(void);
typedef void (*interrupt_clearer_f)(void);

typedef enum
{
    RPI_BASIC_ARM_TIMER_IRQ = (1 << 0),
    RPI_BASIC_ARM_MAILBOX_IRQ = (1 << 1),
    RPI_BASIC_ARM_DOORBELL_0_IRQ = (1 << 2),
    RPI_BASIC_ARM_DOORBELL_1_IRQ = (1 << 3),
    RPI_BASIC_GPU_0_HALTED_IRQ = (1 << 4),
    RPI_BASIC_GPU_1_HALTED_IRQ = (1 << 5),
    RPI_BASIC_ACCESS_ERROR_1_IRQ = (1 << 6),
    RPI_BASIC_ACCESS_ERROR_0_IRQ = (1 << 7)
} irq_number_t;

typedef struct
{
    volatile uint32_t IRQ_basic_pending;
    volatile uint32_t IRQ_pending_1;
    volatile uint32_t IRQ_pending_2;
    volatile uint32_t FIQ_control;
    volatile uint32_t Enable_IRQs_1;
    volatile uint32_t Enable_IRQs_2;
    volatile uint32_t Enable_Basic_IRQs;
    volatile uint32_t Disable_IRQs_1;
    volatile uint32_t Disable_IRQs_2;
    volatile uint32_t Disable_Basic_IRQs;
} rpi_irq_controller_t;

extern rpi_irq_controller_t *RPI_GetIrqController(void);
extern void _enable_interrupts();

__inline__ int INTERRUPTS_ENABLED(void)
{
    int res;
    __asm__ __volatile__("mrs %[res], CPSR"
                         : [res] "=r"(res)::);
    return ((res >> 7) & 1) == 0;
}

__inline__ void ENABLE_INTERRUPTS(void)
{
    if (!INTERRUPTS_ENABLED())
    {
        uart_puts("\n int not enabled. enabling");
        __asm__ __volatile__("cpsie i");
        // _enable_interrupts();
        // __asm__ __volatile__("cpsie i");
    }
}

__inline__ void DISABLE_INTERRUPTS(void)
{
    if (INTERRUPTS_ENABLED())
    {
        uart_puts("\n int  enabled. disabling");
        __asm__ __volatile__("cpsid i");
    }
}

void interrupts_init(void);
void register_irq_handler(irq_number_t irq_num, interrupt_handler_f handler, interrupt_clearer_f clearer);
void unregister_irq_handler(irq_number_t irq_num);

#endif
