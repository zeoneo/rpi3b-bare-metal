
#include <stdint.h>

// #include "rpi-armtimer.h"
#include "rpi-base.h"
#include "rpi-interrupts.h"
#include "uart0.h"

/** @brief The BCM2835 Interupt controller peripheral at it's base address */
static rpi_irq_controller_t *rpiIRQController =
    (rpi_irq_controller_t *)RPI_INTERRUPT_CONTROLLER_BASE;

/**
    @brief Return the IRQ Controller register set
*/
rpi_irq_controller_t *RPI_GetIrqController(void)
{
    return rpiIRQController;
}

void irq_handler(void)
{
    uart_puts("interrupt_vector Interrupt Occurred");
}
