#include<graphics/opengl_es.h>
#include<graphics/v3d.h>
#include<graphics/gpu_mem_util.h>
#include <plibc/stdio.h>

#include<stdint.h>
#include<stdbool.h>
#include<math.h>


/* Our vertex shader text we will compile */
char vShaderStr[] =
"uniform mat4 u_rotateMx;  \n"
"attribute vec4 a_position;   \n"
"attribute vec2 a_texCoord;   \n"
"varying vec2 v_texCoord;     \n"
"void main()                  \n"
"{                            \n"
"   gl_Position = u_rotateMx * a_position; \n"
"   v_texCoord = a_texCoord;  \n"
"}                            \n";

/* Our fragment shader text we will compile */
char fShaderStr[] =
"precision mediump float;                            \n"
"varying vec2 v_texCoord;                            \n"
"uniform sampler2D s_texture;                        \n"
"void main()                                         \n"
"{                                                   \n"
"  gl_FragColor = texture2D( s_texture, v_texCoord );\n"
"}                                                   \n";

#define v3d ((volatile __attribute__((aligned(4))) uint32_t *)(uintptr_t)(V3D_BASE))

extern void* GPUaddrToARMaddr(uint32_t bus_addr);

// int32_t load_shader(int32_t shaderType) {

// }

float cosTheta = 1.0f;
//float sinTheta = 0.0f;
float angle = 0.0f;

static int32_t rotate_x(uint_fast32_t xOld)
{
	return (int32_t)(xOld * cosTheta);
}

static int32_t rotate_y(uint_fast32_t yOld)
{
	return (int32_t)(yOld * cosTheta);
}

void do_rotate(float delta) {
	angle += delta;
	if (angle >= (3.1415926384 * 2))
		angle -= (3.1415926384 * 2);
	cosTheta = cosf(angle);
}

void test_triangle (uint16_t renderWth, uint16_t renderHt, uint32_t renderBufferAddr) {
    #define BUFFER_VERTEX_INDEX 0x70
    #define BUFFER_SHADER_OFFSET 0x80
    #define BUFFER_VERTEX_DATA 0x100
    #define BUFFER_TILE_STATE 0x200
    #define BUFFER_TILE_DATA 0x6000
    #define BUFFER_RENDER_CONTROL 0xe200
    #define BUFFER_FRAGMENT_SHADER 0xfe00
    #define BUFFER_FRAGMENT_UNIFORM 0xff00

    uint32_t handle = v3d_mem_alloc(0x800000, 0x1000, MEM_FLAG_COHERENT | MEM_FLAG_ZERO);
    if(handle == 0) {
        printf("Could not allocate memory in gpu \n");
        return;
    }

    uint32_t bus_addr = v3d_mem_lock(handle);
	uint32_t arm_addr = bus_addr & (~0xC0000000);

	printf("handle :%x, bus_addr:%x arm_addr:%x", handle, bus_addr, arm_addr);
    uint8_t *list = (uint8_t *)arm_addr;
    uint8_t *p = list;

    uint_fast32_t binWth = (renderWth + 63) / 64; // Tiles across
    uint_fast32_t binHt = (renderHt + 63) / 64;   // Tiles down

    emit_uint8_t(&p, GL_TILE_BINNING_CONFIG);		 // tile binning config control
	emit_uint32_t(&p, bus_addr + BUFFER_TILE_DATA);  // tile allocation memory address
	emit_uint32_t(&p, 0x4000);						 // tile allocation memory size
	emit_uint32_t(&p, bus_addr + BUFFER_TILE_STATE); // Tile state data address
	emit_uint8_t(&p, binWth);						 // renderWidth/64
	emit_uint8_t(&p, binHt);						 // renderHt/64
	emit_uint8_t(&p, 0x04);							 // config


    // Start tile binning.
	emit_uint8_t(&p, GL_START_TILE_BINNING); // Start binning command

	// Primitive type
	emit_uint8_t(&p, GL_PRIMITIVE_LIST_FORMAT);
	emit_uint8_t(&p, 0x32); // 16 bit triangle

	// Clip Window
	emit_uint8_t(&p, GL_CLIP_WINDOW); // Clip window
	emit_uint16_t(&p, 0);			  // 0
	emit_uint16_t(&p, 0);			  // 0
	emit_uint16_t(&p, renderWth);	 // width
	emit_uint16_t(&p, renderHt);	  // height

	// GL State
	emit_uint8_t(&p, GL_CONFIG_STATE);
	emit_uint8_t(&p, 0x03); // enable both foward and back facing polygons
	emit_uint8_t(&p, 0x00); // depth testing disabled
	emit_uint8_t(&p, 0x02); // enable early depth write

	// Viewport offset
	emit_uint8_t(&p, GL_VIEWPORT_OFFSET); // Viewport offset
	emit_uint16_t(&p, 0);				  // 0
	emit_uint16_t(&p, 0);				 

    // The triangle
	// No Vertex Shader state (takes pre-transformed vertexes so we don't have to supply a working coordinate shader.)
	emit_uint8_t(&p, GL_NV_SHADER_STATE);
	emit_uint32_t(&p, bus_addr + BUFFER_SHADER_OFFSET); // Shader Record

	// primitive index list
	emit_uint8_t(&p, GL_INDEXED_PRIMITIVE_LIST);	   // Indexed primitive list command
	emit_uint8_t(&p, PRIM_TRIANGLE);				   // 8bit index, triangles
	emit_uint32_t(&p, 9);							   // Length
	emit_uint32_t(&p, bus_addr + BUFFER_VERTEX_INDEX); // address
	emit_uint32_t(&p, 6);							   // Maximum index

	// End of bin list
	// So Flush
	emit_uint8_t(&p, GL_FLUSH_ALL_STATE);
	// Nop
	emit_uint8_t(&p, GL_NOP);
	// Halt
	emit_uint8_t(&p, GL_HALT);

	int length = p - list;

	// Okay now we need Shader Record to buffer
	p = list + BUFFER_SHADER_OFFSET;
	emit_uint8_t(&p, 0x01);								   // flags
	emit_uint8_t(&p, 6 * 4);							   // stride
	emit_uint8_t(&p, 0xcc);								   // num uniforms (not used)
	emit_uint8_t(&p, 3);								   // num varyings
	emit_uint32_t(&p, bus_addr + BUFFER_FRAGMENT_SHADER);  // Fragment shader code
	emit_uint32_t(&p, bus_addr + BUFFER_FRAGMENT_UNIFORM); // Fragment shader uniforms
	emit_uint32_t(&p, bus_addr + BUFFER_VERTEX_DATA);	  // Vertex Data

	/* Setup triangle vertices from OpenGL tutorial which used this */
	// fTriangle[0] = -0.4f; fTriangle[1] = 0.1f; fTriangle[2] = 0.0f;
	// fTriangle[3] = 0.4f; fTriangle[4] = 0.1f; fTriangle[5] = 0.0f;
	// fTriangle[6] = 0.0f; fTriangle[7] = 0.7f; fTriangle[8] = 0.0f;
	uint_fast32_t centreX = renderWth / 2;									// triangle centre x
	uint_fast32_t centreY = (uint_fast32_t)(0.4f * (renderHt / 2));			// triangle centre y
	uint_fast32_t half_shape_wth = (uint_fast32_t)(0.4f * (renderWth / 2)); // Half width of triangle
	uint_fast32_t half_shape_ht = (uint_fast32_t)(0.3f * (renderHt / 2));   // half height of tringle

	// Vertex Data
	p = list + BUFFER_VERTEX_DATA;
	// Vertex: Top, vary red
	emit_uint16_t(&p, centreX << 4);				   // X in 12.4 fixed point
	emit_uint16_t(&p, (centreY - half_shape_ht) << 4); // Y in 12.4 fixed point
	emit_float(&p, 1.0f);							   // Z
	emit_float(&p, 1.0f);							   // 1/W
	emit_float(&p, 1.0f);							   // Varying 0 (Red)
	emit_float(&p, 0.0f);							   // Varying 1 (Green)
	emit_float(&p, 0.0f);							   // Varying 2 (Blue)

	// Vertex: bottom left, vary blue
	emit_uint16_t(&p, (centreX - rotate_x(half_shape_wth)) << 4); // X in 12.4 fixed point
	emit_uint16_t(&p, (centreY + half_shape_ht) << 4);			  // Y in 12.4 fixed point
	emit_float(&p, 1.0f);										  // Z
	emit_float(&p, 1.0f);										  // 1/W
	emit_float(&p, 0.0f);										  // Varying 0 (Red)
	emit_float(&p, 0.0f);										  // Varying 1 (Green)
	emit_float(&p, 1.0f);										  // Varying 2 (Blue)

	// Vertex: bottom right, vary green
	emit_uint16_t(&p, (centreX + rotate_x(half_shape_wth)) << 4); // X in 12.4 fixed point
	emit_uint16_t(&p, (centreY + half_shape_ht) << 4);			  // Y in 12.4 fixed point
	emit_float(&p, 1.0f);										  // Z
	emit_float(&p, 1.0f);										  // 1/W
	emit_float(&p, 0.0f);										  // Varying 0 (Red)
	emit_float(&p, 1.0f);										  // Varying 1 (Green)
	emit_float(&p, 0.0f);										  // Varying 2 (Blue)

	/* Setup quad vertices from OpenGL tutorial which used this */
	// fQuad[0] = -0.2f; fQuad[1] = -0.1f; fQuad[2] = 0.0f;
	// fQuad[3] = -0.2f; fQuad[4] = -0.6f; fQuad[5] = 0.0f;
	// fQuad[6] = 0.2f; fQuad[7] = -0.1f; fQuad[8] = 0.0f;
	// fQuad[9] = 0.2f; fQuad[10] = -0.6f; fQuad[11] = 0.0f;
	centreY = (uint_fast32_t)(1.35f * (renderHt / 2)); // quad centre y

	// Vertex: Top, left  vary blue
	emit_uint16_t(&p, (centreX - half_shape_wth) << 4);			 // X in 12.4 fixed point
	emit_uint16_t(&p, (centreY - rotate_y(half_shape_ht)) << 4); // Y in 12.4 fixed point
	emit_float(&p, 1.0f);										 // Z
	emit_float(&p, 1.0f);										 // 1/W
	emit_float(&p, 0.0f);										 // Varying 0 (Red)
	emit_float(&p, 0.0f);										 // Varying 1 (Green)
	emit_float(&p, 1.0f);										 // Varying 2 (Blue)

	// Vertex: bottom left, vary Green
	emit_uint16_t(&p, (centreX - half_shape_wth) << 4);			 // X in 12.4 fixed point
	emit_uint16_t(&p, (centreY + rotate_y(half_shape_ht)) << 4); // Y in 12.4 fixed point
	emit_float(&p, 1.0f);										 // Z
	emit_float(&p, 1.0f);										 // 1/W
	emit_float(&p, 0.0f);										 // Varying 0 (Red)
	emit_float(&p, 1.0f);										 // Varying 1 (Green)
	emit_float(&p, 0.0f);										 // Varying 2 (Blue)

	// Vertex: top right, vary red
	emit_uint16_t(&p, (centreX + half_shape_wth) << 4);			 // X in 12.4 fixed point
	emit_uint16_t(&p, (centreY - rotate_y(half_shape_ht)) << 4); // Y in 12.4 fixed point
	emit_float(&p, 1.0f);										 // Z
	emit_float(&p, 1.0f);										 // 1/W
	emit_float(&p, 1.0f);										 // Varying 0 (Red)
	emit_float(&p, 0.0f);										 // Varying 1 (Green)
	emit_float(&p, 0.0f);										 // Varying 2 (Blue)

	// Vertex: bottom right, vary yellow
	emit_uint16_t(&p, (centreX + half_shape_wth) << 4);			 // X in 12.4 fixed point
	emit_uint16_t(&p, (centreY + rotate_y(half_shape_ht)) << 4); // Y in 12.4 fixed point
	emit_float(&p, 1.0f);										 // Z
	emit_float(&p, 1.0f);										 // 1/W
	emit_float(&p, 0.0f);										 // Varying 0 (Red)
	emit_float(&p, 1.0f);										 // Varying 1 (Green)
	emit_float(&p, 1.0f);										 // Varying 2 (Blue)

	// Vertex list
	p = list + BUFFER_VERTEX_INDEX;
	emit_uint8_t(&p, 0); // tri - top
	emit_uint8_t(&p, 1); // tri - bottom left
	emit_uint8_t(&p, 2); // tri - bottom right

	emit_uint8_t(&p, 3); // quad - top left
	emit_uint8_t(&p, 4); // quad - bottom left
	emit_uint8_t(&p, 5); // quad - top right

	emit_uint8_t(&p, 4); // quad - bottom left
	emit_uint8_t(&p, 6); // quad - bottom right
	emit_uint8_t(&p, 5); // quad - top right

	// fragment shader
	p = list + BUFFER_FRAGMENT_SHADER;
	emit_uint32_t(&p, 0x958e0dbf);
	emit_uint32_t(&p, 0xd1724823); /* mov r0, vary; mov r3.8d, 1.0 */
	emit_uint32_t(&p, 0x818e7176);
	emit_uint32_t(&p, 0x40024821); /* fadd r0, r0, r5; mov r1, vary */
	emit_uint32_t(&p, 0x818e7376);
	emit_uint32_t(&p, 0x10024862); /* fadd r1, r1, r5; mov r2, vary */
	emit_uint32_t(&p, 0x819e7540);
	emit_uint32_t(&p, 0x114248a3); /* fadd r2, r2, r5; mov r3.8a, r0 */
	emit_uint32_t(&p, 0x809e7009);
	emit_uint32_t(&p, 0x115049e3); /* nop; mov r3.8b, r1 */
	emit_uint32_t(&p, 0x809e7012);
	emit_uint32_t(&p, 0x116049e3); /* nop; mov r3.8c, r2 */
	emit_uint32_t(&p, 0x159e76c0);
	emit_uint32_t(&p, 0x30020ba7); /* mov tlbc, r3; nop; thrend */
	emit_uint32_t(&p, 0x009e7000);
	emit_uint32_t(&p, 0x100009e7); /* nop; nop; nop */
	emit_uint32_t(&p, 0x009e7000);
	emit_uint32_t(&p, 0x500009e7); /* nop; nop; sbdone */

	// Render control list
	p = list + BUFFER_RENDER_CONTROL;

	// Clear colors
	emit_uint8_t(&p, GL_CLEAR_COLORS);
	emit_uint32_t(&p, 0xff000000); // Opaque Black
	emit_uint32_t(&p, 0xff000000); // 32 bit clear colours need to be repeated twice
	emit_uint32_t(&p, 0);
	emit_uint8_t(&p, 0);

	// Tile Rendering Mode Configuration
	emit_uint8_t(&p, GL_TILE_RENDER_CONFIG);

	emit_uint32_t(&p, renderBufferAddr); // render address

	emit_uint16_t(&p, renderWth); // width
	emit_uint16_t(&p, renderHt);  // height
	emit_uint8_t(&p, 0x04);		  // framebuffer mode (linear rgba8888)
	emit_uint8_t(&p, 0x00);

	// Do a store of the first tile to force the tile buffer to be cleared
	// Tile Coordinates
	emit_uint8_t(&p, GL_TILE_COORDINATES);
	emit_uint8_t(&p, 0);
	emit_uint8_t(&p, 0);
	// Store Tile Buffer General
	emit_uint8_t(&p, GL_STORE_TILE_BUFFER);
	emit_uint16_t(&p, 0); // Store nothing (just clear)
	emit_uint32_t(&p, 0); // no address is needed

	// Link all binned lists together
	for (int x = 0; x < binWth; x++)
	{
		for (int y = 0; y < binHt; y++)
		{

			// Tile Coordinates
			emit_uint8_t(&p, GL_TILE_COORDINATES);
			emit_uint8_t(&p, x);
			emit_uint8_t(&p, y);

			// Call Tile sublist
			emit_uint8_t(&p, GL_BRANCH_TO_SUBLIST);
			emit_uint32_t(&p, bus_addr + BUFFER_TILE_DATA + (y * binWth + x) * 32);

			// Last tile needs a special store instruction
			if (x == (binWth - 1) && (y == binHt - 1))
			{
				// Store resolved tile color buffer and signal end of frame
				emit_uint8_t(&p, GL_STORE_MULTISAMPLE_END);
			}
			else
			{
				// Store resolved tile color buffer
				emit_uint8_t(&p, GL_STORE_MULTISAMPLE);
			}
		}
	}

	int render_length = p - (list + BUFFER_RENDER_CONTROL);

	// Run our control list
	v3d[V3D_BFC] = 1; // reset binning frame count
	v3d[V3D_CT0CA] = bus_addr;
	v3d[V3D_CT0EA] = bus_addr + length;

	printf("Wait for control list to execute\n");
	// Wait for control list to execute
	while (v3d[V3D_CT0CS] & 0x20)
		;

	printf("wait for binning to finish v3d[V3D_BFC]: %x \n", v3d[V3D_BFC]);
	// wait for binning to finish
	while (v3d[V3D_BFC] == 0)
	{
	}

	printf("Stop Thread \n");
	// stop the thread
	v3d[V3D_CT0CS] = 0x20;

	// Run our render
	v3d[V3D_RFC] = 1; // reset rendering frame count
	v3d[V3D_CT1CA] = bus_addr + BUFFER_RENDER_CONTROL;
	v3d[V3D_CT1EA] = bus_addr + BUFFER_RENDER_CONTROL + render_length;

	printf("Wait for render to execute \n");
	// Wait for render to execute
	while (v3d[V3D_CT1CS] & 0x20)
		;

	printf("wait for render to finish \n");
	// wait for render to finish
	while (v3d[V3D_RFC] == 0)
	{
	}

	printf(" stop the thread \n");
	// stop the thread
	v3d[V3D_CT1CS] = 0x20;

	printf(" Release resources \n");
	// Release resources
	v3d_mem_unlock(handle);
	v3d_mem_free(handle);
	printf(" Returning  \n");
}