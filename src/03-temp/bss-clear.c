extern int __bss_start;
extern int __bss_end;

extern void kernel_main( unsigned int r0, unsigned int r1, unsigned int atags );

void _clear_bss( unsigned int r0, unsigned int r1, unsigned int r2 )
{
    int* bss = &__bss_start;
    int* bss_end = &__bss_end;

    while( bss < bss_end )
        *bss++ = 0;

    kernel_main( r0, r1, r2 );
    while(1);
}
