
#ifndef _STDLIB_H
#define _STDLIB_H

#ifdef __cplusplus
extern "C"
{
#endif

#include <stdint.h>
#include <stddef.h>

void* memset(void* bufptr, uint8_t value, size_t size);

#ifdef __cplusplus
}
#endif

#endif