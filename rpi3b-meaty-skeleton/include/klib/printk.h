#ifndef _KERNEL_PRINTK_H
#define _KERNEL_PRINTK_H

#ifdef __cplusplus
extern "C"
{
#endif

int printk(const char *format, ...);

#ifdef __cplusplus
}
#endif

#endif