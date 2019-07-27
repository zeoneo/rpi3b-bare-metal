#ifndef _RAMDISK_FS_H
#define _RAMDISK_FS_H

#ifdef __cplusplus
extern "C"
{
#endif

#include<stdint.h>

uint32_t initialize_ramdisk(const uint8_t *initrd_base, uint32_t initrd_size);
int32_t ramdisk_read(uint32_t offset, uint32_t length, char *dest);

#ifdef __cplusplus
}
#endif

#endif