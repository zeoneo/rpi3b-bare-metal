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
#include <graphics/v3d.h>
#include <graphics/pi_console.h>
#include <graphics/opengl_es.h>

extern uint32_t __kernel_end;

typedef struct {
    float r;
    float g;
    float b;
    float a;
} colour_t;
volatile int calculate_frame_count = 1;
#define COLOUR_DELTA    0.05
void screen_me(uint32_t fb_addr, uint32_t width, uint32_t height,uint32_t depth, uint32_t pitch1);
void kernel_main(uint32_t r0, uint32_t r1, uint32_t atags)
{
	// Declare as unused
	(void)r0;
	(void)r1;
	(void)atags;

	printf("\n-----------------Kernel Started Dude........................\n");
	uart_init();
	// interrupts_init();

	timer_init();
	// mem_init();
	printf("\n Kernel End: 0x%x \n", &__kernel_end);
	// initialize_virtual_memory();
	// uart_puts("\n Hello virtual memory world 123 \n ");

	// show_dma_demo();
	// udelay(4579 * 1000 * 10);
	// printf("\n 64 bit: %lx", 0x1234567812340000);

	// UsbInitialise();
	// uint32_t address = KeyboardGetAddress(0);
	// uint32_t count = MouseCount();
	// uint32_t mouse_address = MouseGetAddress(0);

	
	// if(initialize_fat()) {
	// 	printf("-------Successfully Initialized FAT----------\n");
	// 	// print_root_directory_info();
	// } else {
	// 	printf("-------Failed to initialize FAT----------\n");
	// }
	
	if(init_v3d()) {
		printf("-------Successfully Initialized QPU----------\n");
		uint32_t width =0, height =0, depth = 0, pitch =0;
		get_console_width_height_depth(&width, &height, &depth, &pitch);
		uint32_t fb_addr = get_console_frame_buffer(width, height, depth);
		fb_addr = fb_addr & (~0xC0000000);
		uint32_t *fb = (uint32_t *)fb_addr;
		for(uint32_t x = 0; x < width/2; x++) {
			for(uint32_t y=0; y< height/2; y++) {
				*fb  = 0x00FFFFFF;
				// printf("%x", *(fb));
				fb++;
			}
		}
		for(uint32_t x = width/2; x < width; x++) {
			for(uint32_t y=height/2; y< height; y++) {
				*fb  = 0x00FF000F; // printf("%x", *(fb));
				fb++;
			}
		}
		console_puts("Hello World \n Hi there this is prakash \n");
		printf("\n");
		// test_triangle(width, height, fb_addr);
		// while (1){
		// 	do_rotate(0.1f);
		// 	test_triangle(width, height, fb_addr);
		// }
		// screen_me(fb_addr, width, height, depth, pitch);
	} else {
		printf("-------Failed to initialize QPU----------\n");
	}

	
	while (1)
	{
	}
}


void screen_me(uint32_t fb_addr, uint32_t width, uint32_t height,uint32_t depth, uint32_t pitch1) {
	uint8_t *fb =  (uint8_t *) fb_addr;
	uint32_t bpp = depth;
	printf("fb_addr :%x \n", fb_addr);

		uint32_t x, y, pitch = pitch1;
		uint32_t pixel_offset;
		uint32_t r, g, b, a;
		float cd = COLOUR_DELTA;
		uint32_t frame_count = 0;

	colour_t current_colour;
	current_colour.r = 0;
	current_colour.g = 0;
	current_colour.b = 0;
	current_colour.a = 1.0;
	while( 1 )
	{
		current_colour.r = 0;

		/* Produce a colour spread across the screen */
		for( y = 0; y < height ; y++ )
		{
			current_colour.r += ( 1.0 / height );
			current_colour.b = 0;

			for( x = 0; x < width; x++ )
			{
				pixel_offset = ( x * ( bpp >> 3 ) ) + ( y * pitch );

				r = (int)( current_colour.r * 0xFF ) & 0xFF;
				g = (int)( current_colour.g * 0xFF ) & 0xFF;
				b = (int)( current_colour.b * 0xFF ) & 0xFF;
				a = (int)( current_colour.b * 0xFF ) & 0xFF;

				if( bpp == 32 )
				{
					/* Four bytes to write */
					fb[ pixel_offset++ ] = r;
					fb[ pixel_offset++ ] = g;
					fb[ pixel_offset++ ] = b;
					fb[ pixel_offset++ ] = a;
				}
				else if( bpp == 24 )
				{
					/* Three bytes to write */
					fb[ pixel_offset++ ] = r;
					fb[ pixel_offset++ ] = g;
					fb[ pixel_offset++ ] = b;
				}
				else if( bpp == 16 )
				{
					/* Two bytes to write */
					/* Bit pack RGB565 into the 16-bit pixel offset */
					*(unsigned short*)&fb[pixel_offset] = ( (r >> 3) << 11 ) | ( ( g >> 2 ) << 5 ) | ( b >> 3 );
				}
				else
				{
					/* Palette mode. TODO: Work out a colour scheme for
					packing rgb into an 8-bit palette! */
				}

				current_colour.b += ( 1.0 / width );
			}
		}

		/* Scroll through the green colour */
		current_colour.g += cd;
		if( current_colour.g > 1.0 )
		{
			current_colour.g = 1.0;
			cd = -COLOUR_DELTA;
		}
		else if( current_colour.g < 0.0 )
		{
			current_colour.g = 0.0;
			cd = COLOUR_DELTA;
		}

		frame_count++;
		if( calculate_frame_count )
		{
			calculate_frame_count = 0;

			/* Number of frames in a minute, divided by seconds per minute */
			// float fps = (float)frame_count / 60;
			printf( "Frames: %d \r\n", frame_count );

			frame_count = 0;
		}
	}
}