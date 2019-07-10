ENTRY (_start)
load_start = 0x8000;
/* virt_start = 0xC0000000; //3GB */
virt_start = 0x80000000; //2GB
SECTIONS
{
    . = load_start;
    .lowtext :
    {
        *(.lowtext)
    }
    . = . + virt_start;
    .text
    { 
        code_start = .;
        *(.text*)
    }
    /* other higher half sections goes here */
}