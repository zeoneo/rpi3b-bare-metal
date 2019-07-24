#include <stddef.h>
#include <stdint.h>
#include <klib/printk.h>

#include <device/keyboard.h>
#include <device/mouse.h>
#include <device/uart0.h>
#include <device/dma.h>
#include <device/usbd.h>
#include <fs/fat.h>
#include <kernel/rpi-armtimer.h>
#include <kernel/rpi-interrupts.h>
#include <kernel/systimer.h>
#include <mem/physmem.h>
#include <mem/virtmem.h>
// #include <graphics/v3d.h>
// #include <graphics/pi_console.h>
// #include <graphics/opengl_es.h>
// #include <graphics/opengl_es2.h>

extern uint32_t __kernel_end;
extern uint32_t __text_boot_start;
extern uint32_t __text_boot_end;
extern uint32_t __text_boot_end_aligned;
extern uint32_t __first_lvl_tbl_base;
extern uint32_t __second_lvl_tbl_base;
extern uint32_t __second_lvl_tbl_end;

// typedef struct {
//     float r;
//     float g;
//     float b;
//     float a;
// } colour_t;
// volatile int calculate_frame_count = 1;

// #define _countof(_Array) (sizeof(_Array) / sizeof(_Array[0])) 

// #define COLOUR_DELTA    0.05

// static uint32_t shader1[18] = {  // Vertex Color Shader
// 		0x958e0dbf, 0xd1724823,   /* mov r0, vary; mov r3.8d, 1.0 */
// 		0x818e7176, 0x40024821,   /* fadd r0, r0, r5; mov r1, vary */
//         0x818e7376, 0x10024862,   /* fadd r1, r1, r5; mov r2, vary */
// 		0x819e7540, 0x114248a3,   /* fadd r2, r2, r5; mov r3.8a, r0 */
// 	    0x809e7009, 0x115049e3,   /* nop; mov r3.8b, r1 */
// 		0x809e7012, 0x116049e3,   /* nop; mov r3.8c, r2 */
// 		0x159e76c0, 0x30020ba7,   /* mov tlbc, r3; nop; thrend */
// 		0x009e7000, 0x100009e7,   /* nop; nop; nop */
// 		0x009e7000, 0x500009e7,   /* nop; nop; sbdone */
// };

// static uint32_t shader2[12] = { // Fill Color Shader
// 		0x009E7000, 0x100009E7,   // nop; nop; nop
// 		0xFFFFFFFF,	0xE0020BA7,	  // ldi tlbc, RGBA White
// 		0x009E7000, 0x500009E7,   // nop; nop; sbdone
// 		0x009E7000, 0x300009E7,   // nop; nop; thrend
// 		0x009E7000, 0x100009E7,   // nop; nop; nop
// 		0x009E7000, 0x100009E7,   // nop; nop; nop
// };

// static RENDER_STRUCT scene = { 0 };

void kernel_main(uint32_t r0, uint32_t r1, uint32_t atags)
{
	// Declare as unused
	(void)r0;
	(void)r1;
	(void)atags;

	uart_init();
	printk("\n-----------------Kernel Started Dude--------------------\n");
	mem_init();
	printk("\n Num of free Pages: %d \n", get_num_of_free_pages());
	interrupts_init();
	timer_init();
	printk("Timer Ticks Now: %d \n", timer_getTickCount32());
	timer_set(3000);

	// printk("Timer Ticks Now: %d \n", timer_getTickCount32());
	virt_mem_init();
	get_new_vpage(0x8c000000);
	printk("\n Num of free Pages: %d \n", get_num_of_free_pages());

	printk("\n-----------------Kernel Init Completed--------------------\n");
	while (1)
	{
	}
}

