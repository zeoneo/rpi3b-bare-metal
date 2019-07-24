#include <stdint.h>
extern uint32_t __bss_start;
extern uint32_t __bss_end;

extern void kernel_main(uint32_t r0, uint32_t r1, uint32_t atags);

void _clear_bss(uint32_t r0, uint32_t r1, uint32_t r2)
{
    uint32_t *bss = &__bss_start;
    uint32_t *bss_end = &__bss_end;

    while (bss < bss_end)
        *bss++ = 0x0;

    kernel_main(r0, r1, r2);
    while (1)
        ;
}
