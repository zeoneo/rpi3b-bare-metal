ENTRY(_start)
 
SECTIONS
{
	/* Sets current address to 0x8000 */
	. = 0x8000;
	/* Sets start and text_start to the current address (0x8000) */
	__start = .;


	/************************ Text Section ********************/
	__text_start = .;
	.text :
	{
		/* Put boot section first */
		KEEP(*(.text.boot))
		/* Then put .text */
		*(.text)
	}
	/* align to page size */
	. = ALIGN(4096); 
	__text_end = .;
 
	/****************** Read-Only Data Section ********************/
	__rodata_start = .;
	.rodata :
	{
		/* Include all rodata sections */
		*(.rodata)
	}
	. = ALIGN(4096); /* align to page size */
	__rodata_end = .;


	/****************** Data Section ********************/
	__data_start = .;
	.data :
	{
		/* Include all data sections */
		*(.data)
	}
	. = ALIGN(4096); /* align to page size */
	__data_end = .;

	__bss_start = .;
	.bss :
	{
		bss = .;
		*(.bss)
	}
	. = ALIGN(4096); /* align to page size */
	__bss_end = .;
	__end = .;

	/DISCARD/ :
   {
        *(.comment)
        *(.gnu*)
		*(.debug*)
        *(.note*)
        *(.eh_frame*)
   }
}
