
#ifndef _STRING_H
#define _STRING_H

#ifdef __cplusplus
extern "C"
{
#endif

#include <stddef.h>

size_t strlen(const char* str);
int memcmp(const void* aptr, const void* bptr, size_t size);
void* memcpy(void* restrict dstptr, const void* restrict srcptr, size_t size);

#ifdef __cplusplus
}
#endif

#endif