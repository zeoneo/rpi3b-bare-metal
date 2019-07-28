
#ifndef _STDLIB_H
#define _STDLIB_H

#ifdef __cplusplus
extern "C"
{
#endif

#include <stdint.h>
#include <stddef.h>

void* memset(void* bufptr, uint8_t value, size_t size);
int memcmp(const void* aptr, const void* bptr, size_t size);
void* memcpy(void* restrict dstptr, const void* restrict srcptr, size_t size);

#ifdef __cplusplus
}
#endif

#endif