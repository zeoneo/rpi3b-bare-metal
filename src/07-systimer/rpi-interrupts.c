
#include <stdint.h>

// #include "rpi-armtimer.h"
#include "rpi-base.h"
#include "rpi-armtimer.h"
#include "rpi-interrupts.h"
#include "uart0.h"

/** @brief The BCM2835 Interupt controller peripheral at it's base address */
static rpi_irq_controller_t *rpiIRQController =
    (rpi_irq_controller_t *)RPI_INTERRUPT_CONTROLLER_BASE;

volatile int count_irqs = 0;

/**
    @brief Return the IRQ Controller register set
*/
rpi_irq_controller_t *RPI_GetIrqController(void)
{
    return rpiIRQController;
}

/**
    @brief The Reset vector interrupt handler

    This can never be called, since an ARM core reset would also reset the
    GPU and therefore cause the GPU to start running code again until
    the ARM is handed control at the end of boot loading
*/
void __attribute__((interrupt("ABORT"))) reset_vector(void)
{
    uart_puts("ABORT INTERRUPT OCCURRED");
    while (1)
        ;
}

/**
    @brief The undefined instruction interrupt handler

    If an undefined intstruction is encountered, the CPU will start
    executing this function. Just trap here as a debug solution.
*/
void __attribute__((interrupt("UNDEF"))) undefined_instruction_vector(void)
{
    uart_puts("Undef Interrupt Occurred");
    while (1)
        ;
}

/**
    @brief The supervisor call interrupt handler

    The CPU will start executing this function. Just trap here as a debug
    solution.
*/
void __attribute__((interrupt("SWI"))) software_interrupt_vector(void)
{
    uart_puts("Software Interrupt Occurred");
}

void __attribute__((interrupt("ABORT"))) prefetch_abort_vector(void)
{
    uart_puts("prefetch_abort_vector Interrupt Occurred");
    while (1)
        ;
}

void __attribute__((interrupt("ABORT"))) data_abort_vector(void)
{
    uart_puts("data_abort_vector Interrupt Occurred");
    while (1)
        ;
}

void __attribute__((interrupt("IRQ"))) interrupt_vector(void)
{
    RPI_GetArmTimer()->IRQClear = 1;
    uart_puts("interrupt_vector Interrupt Occurred");
    count_irqs++;
    // if(count_irqs >= 13) {
    //     asm(".word 0x34432434fe5");
    // }
}

void __attribute__((interrupt("FIQ"))) fast_interrupt_vector(void)
{
    uart_puts("fast_interrupt_vector Interrupt Occurred");
}
