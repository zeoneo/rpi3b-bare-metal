#ifndef _MINI_UART_H
#define _MINI_UART_H

#ifdef __cplusplus
extern "C"
{
#endif
#include <stdint.h>

void mini_uart_init();
void mini_uart_putc ( uint32_t c );
unsigned int mini_uart_getc ();
void mini_uart_puts(const char *str);
void mini_hexstrings(uint32_t d);


#ifdef __cplusplus
}
#endif

#endif