
#ifndef _STRING_H
#define _STRING_H

#ifdef __cplusplus
extern "C"
{
#endif

#include <stddef.h>
#include <stdint.h>

size_t strlen(const void* str);
int memcmp(const void* aptr, const void* bptr, size_t size);
void* memcpy(void* restrict dstptr, const void* restrict srcptr, size_t size);
void *strncpy(void* restrict dstptr, const void* restrict srcptr, size_t size);
int32_t strncmp(const uint8_t* dstptr, const uint8_t *srcptr, size_t size);
uint8_t *strncat(uint8_t *dest, const uint8_t *src, uint32_t n);
int32_t strlcpy(uint8_t *dest, const uint8_t *src, uint32_t n);

#ifdef __cplusplus
}
#endif

#endif