#include <stdint.h>

extern uint32_t __code_start;
extern uint32_t __text_boot_end_aligned;
extern uint32_t __first_lvl_tbl_base;
extern uint32_t __second_lvl_tbl_base;
extern uint32_t __kernel_end;

extern void start_mmu(uint32_t mmu_base, uint32_t flags);
extern void BOOT_PUT32(uint32_t address, uint32_t value);

void initialize_virtual_memory(void);
uint32_t mmu_section(uint32_t vadd, uint32_t padd, uint32_t flags, uint32_t mmu_base);
uint32_t mmu_page ( uint32_t vadd, uint32_t padd, uint32_t flags, uint32_t first_lvl_base, uint32_t second_lvl_base);

void __attribute__((section (".text.boot"))) initialize_virtual_memory(void)
{

    uint32_t MMUTABLEBASE = (uint32_t)&__text_boot_end_aligned + (uint32_t)&__first_lvl_tbl_base - (uint32_t)&__code_start;
    uint32_t SECOND_TBL_BASE = (uint32_t)&__text_boot_end_aligned + (uint32_t)&__second_lvl_tbl_base - (uint32_t)&__code_start;
    
    // Identity Map boot.text section 
    // TODO: Convert this to loop instead of hardcoding addresses.
    mmu_page(0x8000, 0x8000, 0x0000, MMUTABLEBASE, SECOND_TBL_BASE);

    // Map Higher half kernel
    uint32_t ra = (uint32_t)&__text_boot_end_aligned;
    uint32_t higher_half_kernel_end = (uint32_t)&__text_boot_end_aligned + (uint32_t)&__kernel_end - (uint32_t)&__code_start;
    uint32_t virt_addr = 0x80000000;
    while(1) {
        mmu_section(virt_addr, ra, 0x0000, MMUTABLEBASE);
        ra += 0x00100000; // 4KB
        virt_addr += 0x00100000;
        if(ra >= (higher_half_kernel_end)) {
            break;
        }
    }

    //peripherals
    mmu_section(0x80000000 + 0x3f000000, 0x3f000000, 0x0000, MMUTABLEBASE); //NOT CACHED!
    mmu_section(0x80000000 + 0x3f200000, 0x3f200000, 0x0000, MMUTABLEBASE); //NOT CACHED!

    start_mmu(MMUTABLEBASE, 0x00000005);
}

uint32_t  __attribute__((section (".text.boot"))) mmu_section(uint32_t vadd, uint32_t padd, uint32_t flags, uint32_t mmu_base)
{
    uint32_t table1EntryOffset;
    uint32_t table1EntryAddress;
    uint32_t tableEntry;

    table1EntryOffset = (vadd >> 20) << 2; // get only most significant 12 bits
    //and multiply it by 4 as each entry is 4 Bytes 32bits

    // MMU table base should be at 16KB granularity, Least signficant 12 bits will be always 0. hence do OR with that
    table1EntryAddress = mmu_base | table1EntryOffset;

    // 31: 20  12 bits are physical 12 ms bits from physical address
    tableEntry = (padd & 0xFFF00000);

    // entry[1:0] = 0b10 for section entry
    tableEntry = tableEntry | 2;

    // Access permissions should be 11 for full access entry [11:10] = 0b11
    tableEntry = tableEntry | 0xC00;

    // Add flags
    tableEntry = tableEntry | flags;

    //hexstrings(rb); hexstring(rc);
    // printf("\n entryAddr: 0x%x, entry value:0x%x \n", table1EntryAddress, tableEntry);
    BOOT_PUT32(table1EntryAddress, tableEntry);
    return (0);
}

uint32_t __attribute__((section (".text.boot"))) mmu_page ( uint32_t vadd, uint32_t padd, uint32_t flags, uint32_t first_lvl_base, uint32_t second_lvl_base)
{
    uint32_t ra;
    uint32_t rb;
    uint32_t rc;

    ra= vadd>>20;
    rb= first_lvl_base|(ra<<2);
    rc= (second_lvl_base & 0xFFFFFC00) |1;
    BOOT_PUT32(rb,rc); //first level descriptor
    ra=(vadd>>12)&0xFF;
    rb=(second_lvl_base&0xFFFFFC00)|(ra<<2);
    rc=(padd&0xFFFFF000)|(0xFF0)|flags|2;
    BOOT_PUT32(rb,rc); //second level descriptor
    return(0);
}