#ifndef _ENDIAN_H
#define _ENDIAN_H

#ifdef __cplusplus
extern "C"
{
#endif

#include<stdint.h>

uint32_t htonl(uint32_t x);
uint32_t ntohl(uint32_t x);

#ifdef __cplusplus
}
#endif

#endif

