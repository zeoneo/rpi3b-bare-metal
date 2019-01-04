#include <stdint.h>
#include <plibc/stdio.h>
#include <mem/virtmem.h>
#include <device/uart0.h>

extern uint32_t __kernel_end;
extern uint32_t __first_lvl_tbl_base;
static uint32_t MMUTABLEBASE;
extern void initialize_virtual_memory(void)
{
    MMUTABLEBASE = (uint32_t)&__first_lvl_tbl_base;
    // Identity Map whole kernel area upto __kernel_end in linker.ld
    uint32_t ra;
    for (ra = 0;; ra += 0x00100000)
    {
        mmu_section(ra, ra, 0x0000);
        if (ra >= (uint32_t)&__kernel_end)
        {
            break;
        }
    }

    //peripherals
    mmu_section(0x3f000000, 0x3f000000, 0x0000); //NOT CACHED!
    mmu_section(0x3f200000, 0x3f200000, 0x0000); //NOT CACHED!

    printf("Enabling MMU \n ");
    start_mmu(MMUTABLEBASE, 0x00000005);
}

uint32_t mmu_section(uint32_t vadd, uint32_t padd, uint32_t flags)
{
    uint32_t table1EntryOffset;
    uint32_t table1EntryAddress;
    uint32_t tableEntry;

    table1EntryOffset = (vadd >> 20) << 2; // get only most significant 12 bits
    //and multiply it by 4 as each entry is 4 Bytes 32bits

    // MMU table base should be at 16KB granularity, Least signficant 12 bits will be always 0. hence do OR with that
    table1EntryAddress = MMUTABLEBASE | table1EntryOffset;

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
    PUT32(table1EntryAddress, tableEntry);
    return (0);
}
