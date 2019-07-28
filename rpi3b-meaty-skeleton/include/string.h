
#ifndef _STRING_H
#define _STRING_H

#ifdef __cplusplus
extern "C"
{
#endif

#include <stddef.h>
#include <stdint.h>

int32_t strncmp(const void* dstptr, const void *srcptr, size_t size);
size_t strlen(const void* str);
void *strncpy(void* restrict dstptr, const void* restrict srcptr, size_t size);
void *strncat(void *dest, const void *src, uint32_t n);
uint32_t strlcpy(char *dest, const char *src, uint32_t n);

#ifdef __cplusplus
}
#endif

#endif