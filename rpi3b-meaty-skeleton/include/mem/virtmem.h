#ifndef _VIRT_MEM_H
#define _VIRT_MEM_H
#include <stdint.h>

extern void BOOT_PUT32(uint32_t addr, uint32_t value);
extern void start_mmu(uint32_t, uint32_t);
extern void stop_mmu(void);
extern void invalidate_tlbs(void);
extern void invalidate_caches(void);

void initialize_virtual_memory(void);
uint32_t mmu_section(uint32_t vadd, uint32_t padd, uint32_t flags, uint32_t mmu_base);
uint32_t mmu_page ( uint32_t vadd, uint32_t padd, uint32_t flags, uint32_t first_lvl_base, uint32_t second_lvl_base);

#endif