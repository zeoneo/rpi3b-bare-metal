#include "virtmem.h"
#include "uart0.h"

void initialize_virtual_memory(void)
{
    // unsigned int ra;
    // for (ra = 0x00000000;; ra += 0x00100000)
    // {
    //     mmu_section(ra, ra, 0x0000);
    //     if (ra == 0xFFF00000)
    //         break;
    // }

    // Mapping first 1MB should be sufficient
    mmu_section(0x00000000, 0x00000000, 0x0000);
    mmu_section(0x00100000, 0x00000000, 0x0000);
    mmu_section(0x07f00000, 0x07f00000, 0x0000);

    //peripherals
    mmu_section(0x3f000000, 0x3f000000, 0x0000); //NOT CACHED!
    mmu_section(0x3f200000, 0x3f200000, 0x0000); //NOT CACHED!

    uart_puts("Enabling MMU \n ");
    start_mmu(MMUTABLEBASE, 0x00000005);
    uart_puts("After paging \n ");
}

unsigned int mmu_section(unsigned int vadd, unsigned int padd, unsigned int flags)
{
    unsigned int table1EntryOffset;
    unsigned int table1EntryAddress;
    unsigned int tableEntry;

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

    //hexstrings(rb); hexstring(rc);
    // printf("\n entryAddr: 0x%x, entry value:0x%x \n", table1EntryAddress, tableEntry);
    PUT32(table1EntryAddress, tableEntry);
    return (0);
}
