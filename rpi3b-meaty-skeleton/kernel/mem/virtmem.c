#include <mem/physmem.h>
#include <mem/virtmem.h>
#include <klib/printk.h>

#define PHYS_LIMIT 1024*1024*1024
#define INVALID_PTE 0x0

extern uint32_t __code_start;
extern uint32_t __text_boot_end_aligned;
extern uint32_t __first_lvl_tbl_base;
extern uint32_t __second_lvl_tbl_base;
extern uint32_t __kernel_end;
extern uint32_t __virt_start;

extern void PUT32(uint32_t address, uint32_t value);
extern uint32_t GET32(uint32_t address);
extern void bzero(void *, uint32_t);
extern void invalidate_l1_icache(void);
extern void invalidate_l1_dcache(void);
extern void invalidate_tlb(void);

static uint32_t MMUTABLEBASE;
static uint32_t SECOND_TBL_BASE;
static uint32_t SECOND_TBL_BASE_PHY;

static void map_page ( uint32_t vadd, uint32_t padd, uint32_t flags);
static uint32_t invalidate_page_entry(uint32_t vaddr);

uint32_t virt_mem_init() {
    uint32_t text_boot_end_aligned, first_lvl_tbl_base, second_lvl_tbl_base, virt_start, code_start, kernel_end;
    __asm__ volatile("ldr %[sym_addr], =__text_boot_end_aligned" : [sym_addr] "=r" (text_boot_end_aligned));
    __asm__ volatile("ldr %[sym_addr], =__first_lvl_tbl_base" : [sym_addr] "=r" (first_lvl_tbl_base));
    __asm__ volatile("ldr %[sym_addr], =__second_lvl_tbl_base" : [sym_addr] "=r" (second_lvl_tbl_base));
    __asm__ volatile("ldr %[sym_addr], =__code_start" : [sym_addr] "=r" (code_start));
    __asm__ volatile("ldr %[sym_addr], =__kernel_end" : [sym_addr] "=r" (kernel_end));
    __asm__ volatile("ldr %[sym_addr], =__virt_start" : [sym_addr] "=r" (virt_start));
    MMUTABLEBASE = first_lvl_tbl_base;
    SECOND_TBL_BASE = second_lvl_tbl_base;
    SECOND_TBL_BASE_PHY = text_boot_end_aligned + second_lvl_tbl_base - code_start;
    return 1;
}

uint32_t get_new_vpage(uint32_t vaddr) {
    uint32_t *page_frame = (uint32_t *) alloc_page();
    if(!page_frame) {
        return 0;
    }
    if(PHYS_LIMIT < (uint32_t)page_frame) {
        printk(" Error PHYS: 0x%x \n", PHYS_LIMIT);
        return 0;
    }
    map_page(vaddr, (uint32_t)page_frame, 0x0);
    // Zero out the page. Must do this to poison old data
    bzero((void *)vaddr, PAGE_SIZE);
    return 1;
}

uint32_t free_vpage(uint32_t vaddr) {
    uint32_t * page_frame = (uint32_t *)invalidate_page_entry(vaddr);
    free_page((void *) page_frame);
    return 1;
}

static void map_page ( uint32_t vadd, uint32_t padd, uint32_t flags) {
    uint32_t ra;
    uint32_t rb;
    uint32_t rc;

    ra= vadd>>20;
    rb= MMUTABLEBASE | (ra<<2);
    rc= (SECOND_TBL_BASE_PHY & 0xFFFFFC00) | 1;

    PUT32(rb,rc); //first level descriptor

    ra=(vadd>>12) & 0xFF;
    rb=(SECOND_TBL_BASE & 0xFFFFFC00) | (ra<<2);
    rc=(padd & 0xFFFFF000 ) | (0xFF0) | flags | 2;
    PUT32(rb,rc); //second level descriptor
    invalidate_tlb();
}

static uint32_t invalidate_page_entry(uint32_t vaddr) {
    uint32_t ra;
    uint32_t entryAddress;
    uint32_t entry;

    ra=(vaddr>>12) & 0xFF;
    entryAddress=(SECOND_TBL_BASE & 0xFFFFFC00) | (ra<<2);
    entry = GET32(entryAddress);
    PUT32(entryAddress, INVALID_PTE);
    invalidate_tlb();
    return entry;
}