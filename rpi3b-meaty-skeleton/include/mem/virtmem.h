#ifndef VIRT_MEM_H
#define VIRT_MEM_H

#ifdef __cplusplus
extern "C"
{
#endif

#include <stdint.h>

uint32_t virt_mem_init();
uint32_t get_new_vpage(uint32_t vaddr);
uint32_t free_vpage(uint32_t vaddr);

#ifdef __cplusplus
}
#endif

#endif