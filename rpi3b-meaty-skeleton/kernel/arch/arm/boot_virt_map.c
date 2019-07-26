#include <stdint.h>
#include "boot-uart.h"

extern uint32_t __code_start;
extern uint32_t __text_boot_end_aligned;
extern uint32_t __first_lvl_tbl_base;
extern uint32_t __second_lvl_tbl_base;
extern uint32_t __kernel_end;
extern uint32_t __virt_start;

extern void start_mmu(uint32_t mmu_base, uint32_t flags);
extern void BOOT_PUT32(uint32_t address, uint32_t value);

void initialize_virtual_memory(void);
uint32_t mmu_section(uint32_t vadd, uint32_t padd, uint32_t flags, uint32_t mmu_base);
uint32_t mmu_page ( uint32_t vadd, uint32_t padd, uint32_t flags, uint32_t first_lvl_base, uint32_t second_lvl_base);

void __attribute__((section (".text.boot"))) initialize_virtual_memory(void)
{
    boot_uart_init();
    // boot_uart_putc('\n');
    // boot_uart_putc('\n');
    uint32_t text_boot_end_aligned, first_lvl_tbl_base, second_lvl_tbl_base, virt_start, code_start, kernel_end;
    __asm__ volatile("ldr %[sym_addr], =__text_boot_end_aligned" : [sym_addr] "=r" (text_boot_end_aligned));
    __asm__ volatile("ldr %[sym_addr], =__first_lvl_tbl_base" : [sym_addr] "=r" (first_lvl_tbl_base));
    __asm__ volatile("ldr %[sym_addr], =__second_lvl_tbl_base" : [sym_addr] "=r" (second_lvl_tbl_base));
    __asm__ volatile("ldr %[sym_addr], =__code_start" : [sym_addr] "=r" (code_start));
    __asm__ volatile("ldr %[sym_addr], =__kernel_end" : [sym_addr] "=r" (kernel_end));
    __asm__ volatile("ldr %[sym_addr], =__virt_start" : [sym_addr] "=r" (virt_start));

    // boot_hexstrings(text_boot_end_aligned);
    // boot_hexstrings(first_lvl_tbl_base);
    // boot_hexstrings(second_lvl_tbl_base);
    // boot_hexstrings(code_start);
    // boot_hexstrings(kernel_end);

    uint32_t MMUTABLEBASE = text_boot_end_aligned + first_lvl_tbl_base - code_start;
    uint32_t SECOND_TBL_BASE = text_boot_end_aligned + second_lvl_tbl_base - code_start;
    // boot_hexstrings(SECOND_TBL_BASE);

    // Identity Map boot.text section 
    // TODO: Convert this to loop instead of hardcoding addresses.
    mmu_page(0x0000, 0x0000, 0x0000, MMUTABLEBASE, SECOND_TBL_BASE);
    mmu_page(0x8000, 0x8000, 0x0000, MMUTABLEBASE, SECOND_TBL_BASE);
    

    // Map Higher half kernel
    uint32_t phys_addr = text_boot_end_aligned;
    uint32_t virt_addr = virt_start;
    while(1) {
        mmu_section(virt_addr, phys_addr, 0x0000, MMUTABLEBASE);
        phys_addr += 0x00100000; // 1MB
        virt_addr += 0x00100000;
        if(virt_addr >= (kernel_end)) {
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
    // printk("\n entryAddr: 0x%x, entry value:0x%x \n", table1EntryAddress, tableEntry);
    // boot_uart_putc('e');
    // boot_hexstrings(table1EntryOffset);
    // boot_uart_putc('b');
    // boot_hexstrings(mmu_base);
    // boot_uart_putc('s');
    // boot_hexstrings(table1EntryAddress);
    // boot_uart_putc('\n');

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

    // boot_uart_putc('\n');
    // boot_uart_putc('f');
    // boot_hexstrings(first_lvl_base);
    // boot_uart_putc('b');
    // boot_hexstrings(second_lvl_base);
    // boot_uart_putc('e');
    // boot_hexstrings(rb);
    // boot_uart_putc('\n');
    
    BOOT_PUT32(rb,rc); //second level descriptor
    return(0);
}