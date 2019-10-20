#include <stddef.h>
#include <stdint.h>
#include <plibc/stdio.h>

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
#include <mem/kernel_alloc.h>
#include <graphics/v3d.h>
#include <graphics/pi_console.h>
#include <graphics/opengl_es.h>
#include <graphics/opengl_es2.h>

extern uint32_t __kernel_end;

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
	// mini_uart_init();

	printf("\n-----------------Kernel Started Dude........................\n");
	interrupts_init();

	timer_init();
	// This will flash activity led
	timer_set(500000);
	// mem_init();
	printf("\n Kernel End: 0x%x \n", &__kernel_end);

	mem_alloc_init((uint32_t)&__kernel_end, 0x100000 * 16); // 16 MB
	// uart_puts(" Hello From UART0 \n");
	// mini_uart_puts(" Hello From MINI UART \n");

	// select_alt_func(14, Alt0);
	// select_alt_func(15, Alt0);
	// uart_puts(" Hello From UART0 \n");
	// mini_uart_puts(" Hello From MINI UART \n");
	// initialize_virtual_memory();
	// uart_puts("\n Hello virtual memory world 123 \n ");

	show_dma_demo();
	// enable_wifi();
	// udelay(4579 * 1000 * 10);
	// printf("\n 64 bit: %lx", 0x1234567812340000);

	// UsbInitialise();
	// uint32_t address = KeyboardGetAddress(0);
	// uint32_t count = MouseCount();
	// uint32_t mouse_address = MouseGetAddress(0);

	// if(initialize_fat()) {
	// 	printf("-------Successfully Initialized FAT----------\n");
	// 	print_root_directory_info();
	// 	printf("File size: %d ", get_file_size((uint8_t *)"/kernel8-32.img"));
	// } else {
	// 	printf("-------Failed to initialize FAT----------\n");
	// }

	// if(init_v3d()) {
	// 	printf("-------Successfully Initialized QPU----------\n");
	// 	uint32_t width =0, height =0, depth = 0, pitch =0;
	// 	get_console_width_height_depth(&width, &height, &depth, &pitch);
	// 	width = 640;
	// 	height = 480;
	// 	depth = 32;
	// 	uint32_t fb_addr = get_console_frame_buffer(width, height, depth);
	// 	// Arm Address we got here
	// 	printf("Init scene \n");
	// 	if(v3d_InitializeScene(&scene, width, height)) {
	// 		printf("Initialized the v3d scene \n");
	// 	} else {
	// 		printf("Failed to initialized the v3d scene \n");
	// 	}
	// 	printf("Init scene complete \n");
	// 	if(v3d_AddVertexesToScene(&scene)) {
	// 		printf("Added vertex to scene successfully \n");
	// 	} else {
	// 		printf("Failed Added vertex to scene successfully \n");
	// 	}
	// 	printf("Add vertex complete \n");
	// 	if(v3d_AddShadderToScene(&scene, &shader1[0], _countof(shader1))) {
	// 		printf("Add shaders successfully \n");
	// 	} else {
	// 		printf("Failed Add shaders successfully \n");
	// 	}
	// 	printf("Add shader complete \n");
	// 	if(v3d_SetupRenderControl(&scene, fb_addr)) {
	// 		printf("Set up render success \n");
	// 	} else {
	// 		printf("Failed to set up render \n");
	// 	}
	// 	printf("Add render control complete \n");
	// 	if(v3d_SetupBinningConfig(&scene)) {
	// 		printf(" Set up binning success \n");
	// 	} else {
	// 		printf(" Set up binning failed \n");
	// 	}
	// 	printf("setup binning render complete \n");
	// 	v3d_RenderScene(&scene);
	// 	printf("All done batman .. we have triangles\n");

	// 	// uint32_t arm_fb_addr = fb_addr | 0xC0000000;
	// 	/*
	// 	fb_addr = fb_addr & (~0xC0000000);
	// 	uint32_t *fb = (uint32_t *)fb_addr;
	// 	for(uint32_t x = 0; x < width/2; x++) {
	// 		for(uint32_t y=0; y< height/2; y++) {
	// 			*fb  = 0x00FFFFFF;
	// 			// printf("%x", *(fb));
	// 			fb++;
	// 		}
	// 	}
	// 	for(uint32_t x = width/2; x < width; x++) {
	// 		for(uint32_t y=height/2; y< height; y++) {
	// 			*fb  = 0x00FF000F; // printf("%x", *(fb));
	// 			fb++;
	// 		}
	// 	}
	// 	console_puts("Hello World \n Hi there this is prakash \n");
	// 	printf("\n");
	// 	*/
	// 	// test_triangle(width, height, fb_addr);
	// 	// while (1){
	// 	// 	do_rotate(0.1f);
	// 	// 	test_triangle(width, height, fb_addr);
	// 	// }
	// 	// screen_me(fb_addr, width, height, depth, pitch);
	// } else {
	// 	printf("-------Failed to initialize QPU----------\n");
	// }

	while (1)
	{
	}
}