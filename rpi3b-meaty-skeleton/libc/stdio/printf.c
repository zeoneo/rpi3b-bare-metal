/**
 * @file printf.c
 */
/* Embedded Xinu, Copyright (C) 2009, 2013.  All rights reserved. */

#include <plibc/stdio.h>
#include <stdarg.h>
#include <kernel/uart0.h>

/**
 * @ingroup libxc
 *
 * Print a formatted message to standard output.
 *
 * @param format
 *      The format string.  Not all standard format specifiers are supported by
 *      this implementation.  See _doprnt() for a description of supported
 *      conversion specifications.
 * @param ...
 *      Arguments matching those in the format string.
 *
 * @return
 *      On success, returns the number of characters written.  On write error,
 *      returns a negative value.
 */
#define UNUSED(x) (void)(x)

int fputc(int c, int dev)
{
    UNUSED(dev);
    uart_putc(c);
    return 1;
}

extern int _doprnt(const char *fmt, va_list ap,
                   int (*putc_func)(int, int), int putc_arg);

int printf(const char *format, ...)
{
    va_list ap;
    int ret;

    va_start(ap, format);
    ret = _doprnt(format, ap, fputc, 1);
    va_end(ap);

    return ret;
}
