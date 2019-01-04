#ifndef _VIRT_MEM_H
#define _VIRT_MEM_H

#define MMUTABLEBASE 0x00ff4000

extern void PUT32(unsigned int addr, unsigned int value);
extern unsigned int GET32(unsigned int addr);

extern void start_mmu(unsigned int, unsigned int);
extern void stop_mmu(void);
extern void invalidate_tlbs(void);
extern void invalidate_caches(void);

void initialize_virtual_memory(void);
unsigned int mmu_section(unsigned int vadd, unsigned int padd, unsigned int flags);
#endif