#ifndef PHYS_MEM_H
#define PHYS_MEM_H

#include <stdint.h>
#include <kernel/list.h>

#define PAGE_SIZE 4096

// typedef struct
// {
//     uint8_t allocated : 1;   // This page is allocated to something
//     uint8_t kernel_page : 1; // This page is a part of the kernel
//     uint32_t reserved : 30;
// } page_flags_t;

typedef struct __attribute__((__packed__, aligned(1))) {
    unsigned allocated : 1;							// @95-84	ccc as on SD CSD bits
    unsigned kernel_page : 1;					// @83-80	read_bl_len on SD CSD bits
    unsigned reserved : 6;
} page_flags_t;

typedef struct {
    uint32_t *arm_mem_base_ptr;
    uint32_t arm_mem_in_bytes;
    uint32_t *vc_mem_base_ptr;
    uint32_t vc_mem_in_bytes;
} memory_stats;

typedef struct page
{
    uint16_t vaddr_higher; // The virtual address that maps to this page
    uint8_t vaddr_lower;
    page_flags_t flags;
} page_t;

void mem_init();
uint32_t get_mem_size();
uint32_t get_num_of_free_pages();
void *alloc_page(void);
void free_page(void *ptr);

#endif