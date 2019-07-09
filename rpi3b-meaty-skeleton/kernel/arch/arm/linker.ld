ENTRY(_start)

/* Address where boot section will be linked */
load_start = 0x8000;

/*
 *
 *  Address where kernel will be linked
 *  We will initialize paging and map the kernel physical address to
 *  virt_start address. And then jump to kernel code.
*/
virt_start = 0x80000000; /* 2GB */

SECTIONS
{
    /* Starts at LOADER_ADDR. */
    . = 0x8000;
    __start = .;
    __text_boot_start = .;
    .text.boot :
    {
        KEEP(*(.text.boot))
    }
    __text_boot_end = .;
    . = ALIGN(4096);
    __text_boot_end_aligned = .;
    /* Kernel sections are placed after this */
    . = . + virt_start;

	.text : AT(__code_start - virt_start) { 
        __code_start = .;
        *(.text*)
    }
	.rodata : { *(.rodata) }
	.data : { *(.data) }

    __bss_start = .;
    .bss :
    {
        bss = .;
        *(.bss*)
    }
    __bss_end = .;

    . = . + 131072; /* 128KB for svc stack */
    __svc_stack_base = .;

    . = . + 1024; /* 1024 Bytes for irq stack */
    __irq_stack_base = .;

    . = ALIGN(16384); /* align 16KB for page tables */

    __first_lvl_tbl_base = .;
    . = . + 4096; /* First level page table size */
    __first_lvl_tbl_end = .;

    /* 1KB for each 2nd lvl page table = 4 Bytes * 256 */
    /* How many 2nd lvl tables ? */
    __second_lvl_tbl_base = .;
    . = . + 0x00100000; /* 1MB for 2nd level page tables */
    __second_lvl_tbl_end = .;

    

    __kernel_end = .;
    __kernel_size = __kernel_end - __code_start;
    __physical_first_lvl_table_base = __text_boot_end_aligned + __first_lvl_tbl_base - __code_start;
    __physical_second_lvl_table_end = __text_boot_end_aligned + __second_lvl_tbl_end - __code_start;

}