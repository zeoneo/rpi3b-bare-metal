#include<graphics/opengl_es.h>
#include<graphics/opengl_es2.h>
#include<graphics/v3d.h>
#include<graphics/gpu_mem_util.h>
#include <plibc/stdio.h>

#define v3d ((volatile __attribute__((aligned(4))) uint32_t*)(uintptr_t)(V3D_BASE))
#define ALIGN_128BIT_MASK  0xFFFFFF80
extern uint32_t GPUaddrToARMaddr (uint32_t BUSaddress);


bool v3d_InitializeScene (RENDER_STRUCT* scene, uint32_t renderWth, uint32_t renderHt) {
    if (scene) 
	{
		scene->rendererHandle = v3d_mem_alloc(0x10000, 0x1000, MEM_FLAG_COHERENT | MEM_FLAG_ZERO);
		if (!scene->rendererHandle) return false;
		scene->rendererDataVC4 = v3d_mem_lock(scene->rendererHandle);
		scene->loadpos = scene->rendererDataVC4;					// VC4 load from start of memory

		scene->renderWth = renderWth;								// Render width
		scene->renderHt = renderHt;									// Render height
		scene->binWth = (renderWth + 63) / 64;						// Tiles across 
		scene->binHt = (renderHt + 63) / 64;						// Tiles down 

		scene->tileMemSize = 0x4000;
		scene->tileHandle = v3d_mem_alloc(scene->tileMemSize + 0x4000, 0x1000, MEM_FLAG_COHERENT | MEM_FLAG_ZERO);
		scene->tileStateDataVC4 = v3d_mem_lock(scene->tileHandle);
		scene->tileDataBufferVC4 = scene->tileStateDataVC4 + 0x4000;

		scene->binningHandle = v3d_mem_alloc(0x10000, 0x1000, MEM_FLAG_COHERENT | MEM_FLAG_ZERO);
		scene->binningDataVC4 = v3d_mem_lock(scene->binningHandle);
		return true;
	}
	return false;
}

bool v3d_AddVertexesToScene (RENDER_STRUCT* scene) {
if (scene) 
	{
		scene->vertexVC4 = (scene->loadpos + 127) & ALIGN_128BIT_MASK;	// Hold vertex start adderss .. aligned to 128bits
		uint8_t* p = (uint8_t*)(uintptr_t)GPUaddrToARMaddr(scene->vertexVC4);
		uint8_t* q = p;

		/* Setup triangle vertices from OpenGL tutorial which used this */
		// fTriangle[0] = -0.4f; fTriangle[1] = 0.1f; fTriangle[2] = 0.0f;
		// fTriangle[3] = 0.4f; fTriangle[4] = 0.1f; fTriangle[5] = 0.0f;
		// fTriangle[6] = 0.0f; fTriangle[7] = 0.7f; fTriangle[8] = 0.0f;
		uint_fast32_t centreX = scene->renderWth / 2;							// triangle centre x
		uint_fast32_t centreY = (uint_fast32_t)(0.4f * (scene->renderHt / 2));	// triangle centre y
		uint_fast32_t half_shape_wth = (uint_fast32_t)(0.4f * (scene->renderWth / 2));// Half width of triangle
		uint_fast32_t half_shape_ht = (uint_fast32_t)(0.3f * (scene->renderHt / 2));  // half height of tringle

		// Vertex Data

		// Vertex: Top, vary red
		emit_uint16_t(&p, (centreX) << 4);								// X in 12.4 fixed point
		emit_uint16_t(&p, (centreY - half_shape_ht) << 4);				// Y in 12.4 fixed point
		emit_float(&p, 1.0f);											// Z
		emit_float(&p, 1.0f);											// 1/W
		emit_float(&p, 1.0f);											// Varying 0 (Red)
		emit_float(&p, 0.0f);											// Varying 1 (Green)
		emit_float(&p, 0.0f);											// Varying 2 (Blue)

		// Vertex: bottom left, vary blue
		emit_uint16_t(&p, (centreX - half_shape_wth) << 4);				// X in 12.4 fixed point
		emit_uint16_t(&p, (centreY + half_shape_ht) << 4);				// Y in 12.4 fixed point
		emit_float(&p, 1.0f);											// Z
		emit_float(&p, 1.0f);											// 1/W
		emit_float(&p, 0.0f);											// Varying 0 (Red)
		emit_float(&p, 0.0f);											// Varying 1 (Green)
		emit_float(&p, 1.0f);											// Varying 2 (Blue)

		// Vertex: bottom right, vary green 
		emit_uint16_t(&p, (centreX + half_shape_wth) << 4);				// X in 12.4 fixed point
		emit_uint16_t(&p, (centreY + half_shape_ht) << 4);				// Y in 12.4 fixed point
		emit_float(&p, 1.0f);											// Z
		emit_float(&p, 1.0f);											// 1/W
		emit_float(&p, 0.0f);											// Varying 0 (Red)
		emit_float(&p, 1.0f);											// Varying 1 (Green)
		emit_float(&p, 0.0f);											// Varying 2 (Blue)


		/* Setup triangle vertices from OpenGL tutorial which used this */
		// fQuad[0] = -0.2f; fQuad[1] = -0.1f; fQuad[2] = 0.0f;
		// fQuad[3] = -0.2f; fQuad[4] = -0.6f; fQuad[5] = 0.0f;
		// fQuad[6] = 0.2f; fQuad[7] = -0.1f; fQuad[8] = 0.0f;
		// fQuad[9] = 0.2f; fQuad[10] = -0.6f; fQuad[11] = 0.0f;
		centreY = (uint_fast32_t)(1.35f * (scene->renderHt / 2));				// quad centre y

		// Vertex: Top, left  vary blue
		emit_uint16_t(&p, (centreX - half_shape_wth) << 4);				// X in 12.4 fixed point
		emit_uint16_t(&p, (centreY - half_shape_ht) << 4);				// Y in 12.4 fixed point
		emit_float(&p, 1.0f);											// Z
		emit_float(&p, 1.0f);											// 1/W
		emit_float(&p, 0.0f);											// Varying 0 (Red)
		emit_float(&p, 0.0f);											// Varying 1 (Green)
		emit_float(&p, 1.0f);											// Varying 2 (Blue)

		// Vertex: bottom left, vary Green
		emit_uint16_t(&p, (centreX - half_shape_wth) << 4);				// X in 12.4 fixed point
		emit_uint16_t(&p, (centreY + half_shape_ht) << 4);				// Y in 12.4 fixed point
		emit_float(&p, 1.0f);											// Z
		emit_float(&p, 1.0f);											// 1/W
		emit_float(&p, 0.0f);											// Varying 0 (Red)
		emit_float(&p, 1.0f);											// Varying 1 (Green)
		emit_float(&p, 0.0f);											// Varying 2 (Blue)

		// Vertex: top right, vary red
		emit_uint16_t(&p, (centreX + half_shape_wth) << 4);				// X in 12.4 fixed point
		emit_uint16_t(&p, (centreY - half_shape_ht) << 4);				// Y in 12.4 fixed point
		emit_float(&p, 1.0f);											// Z
		emit_float(&p, 1.0f);											// 1/W
		emit_float(&p, 1.0f);											// Varying 0 (Red)
		emit_float(&p, 0.0f);											// Varying 1 (Green)
		emit_float(&p, 0.0f);											// Varying 2 (Blue)

		// Vertex: bottom right, vary yellow
		emit_uint16_t(&p, (centreX + half_shape_wth) << 4);				// X in 12.4 fixed point
		emit_uint16_t(&p, (centreY + half_shape_ht) << 4);				// Y in 12.4 fixed point
		emit_float(&p, 1.0f);											// Z
		emit_float(&p, 1.0f);											// 1/W
		emit_float(&p, 0.0f);											// Varying 0 (Red)
		emit_float(&p, 1.0f);											// Varying 1 (Green)
		emit_float(&p, 1.0f);											// Varying 2 (Blue)

		scene->num_verts = 7;
		scene->loadpos = scene->vertexVC4 + (p - q);					// Update load position

		scene->indexVertexVC4 = (scene->loadpos + 127) & ALIGN_128BIT_MASK;// Hold index vertex start adderss .. align it to 128 bits
		p = (uint8_t*)(uintptr_t)GPUaddrToARMaddr(scene->indexVertexVC4);
		q = p;

		emit_uint8_t(&p, 0);											// tri - top
		emit_uint8_t(&p, 1);											// tri - bottom left
		emit_uint8_t(&p, 2);											// tri - bottom right

		emit_uint8_t(&p, 3);											// quad - top left
		emit_uint8_t(&p, 4);											// quad - bottom left
		emit_uint8_t(&p, 5);											// quad - top right

		emit_uint8_t(&p, 4);											// quad - bottom left
		emit_uint8_t(&p, 6);											// quad - bottom right
		emit_uint8_t(&p, 5);											// quad - top right
		scene->IndexVertexCt = 9;
		scene->MaxIndexVertex = 6;

		scene->loadpos = scene->indexVertexVC4 + (p - q);				// Move loaad pos to new position
		return true;
	}
	return false;
}

bool v3d_AddShadderToScene (RENDER_STRUCT* scene, uint32_t* frag_shader, uint32_t frag_shader_emits) {
    if (scene)
	{
		scene->shaderStart = (scene->loadpos + 127) & ALIGN_128BIT_MASK;// Hold shader start adderss .. aligned to 127 bits
		uint8_t *p = (uint8_t*)(uintptr_t)GPUaddrToARMaddr(scene->shaderStart);	// ARM address for load
		uint8_t *q = p;												// Hold start address

		for (int i = 0; i < frag_shader_emits; i++)					// For the number of fragment shader emits
			emit_uint32_t(&p, frag_shader[i]);						// Emit fragment shader into our allocated memory

		scene->loadpos = scene->shaderStart + (p - q);				// Update load position

		scene->fragShaderRecStart = (scene->loadpos + 127) & ALIGN_128BIT_MASK;// Hold frag shader start adderss .. .aligned to 128bits
		p = (uint8_t*)(uintptr_t)GPUaddrToARMaddr(scene->fragShaderRecStart);
		q = p;

		// Okay now we need Shader Record to buffer
		emit_uint8_t(&p, 0x01);										// flags
		emit_uint8_t(&p, 6 * 4);									// stride
		emit_uint8_t(&p, 0xcc);										// num uniforms (not used)
		emit_uint8_t(&p, 3);										// num varyings
		emit_uint32_t(&p, scene->shaderStart);						// Shader code address
		emit_uint32_t(&p, 0);										// Fragment shader uniforms (not in use)
		emit_uint32_t(&p, scene->vertexVC4);						// Vertex Data

		scene->loadpos = scene->fragShaderRecStart + (p - q);		// Adjust VC4 load poistion

		return true;
	}
	return false;
}

bool v3d_SetupRenderControl(RENDER_STRUCT* scene, VC4_ADDR renderBufferAddr) {
	if (scene)
	{
		scene->renderControlVC4 = (scene->loadpos + 127) & ALIGN_128BIT_MASK;// Hold render control start adderss .. aligned to 128 bits
		uint8_t *p = (uint8_t*)(uintptr_t)GPUaddrToARMaddr(scene->renderControlVC4); // ARM address for load
		uint8_t *q = p;												// Hold start address

		// Clear colors
		emit_uint8_t(&p, GL_CLEAR_COLORS);
		emit_uint32_t(&p, 0xff000000);								// Opaque Black
		emit_uint32_t(&p, 0xff000000);								// 32 bit clear colours need to be repeated twice
		emit_uint32_t(&p, 0);
		emit_uint8_t(&p, 0);

		// Tile Rendering Mode Configuration
		emit_uint8_t(&p, GL_TILE_RENDER_CONFIG);

		emit_uint32_t(&p, renderBufferAddr);						// render address (will be framebuffer)

		emit_uint16_t(&p, scene->renderWth);						// render width
		emit_uint16_t(&p, scene->renderHt);							// render height
		emit_uint8_t(&p, 0x04);										// framebuffer mode (linear rgba8888)
		emit_uint8_t(&p, 0x00);

		// Do a store of the first tile to force the tile buffer to be cleared
		// Tile Coordinates
		emit_uint8_t(&p, GL_TILE_COORDINATES);
		emit_uint8_t(&p, 0);
		emit_uint8_t(&p, 0);

		// Store Tile Buffer General
		emit_uint8_t(&p, GL_STORE_TILE_BUFFER);
		emit_uint16_t(&p, 0);										// Store nothing (just clear)
		emit_uint32_t(&p, 0);										// no address is needed

		// Link all binned lists together
		for (int x = 0; x < scene->binWth; x++) {
			for (int y = 0; y < scene->binHt; y++) {

				// Tile Coordinates
				emit_uint8_t(&p, GL_TILE_COORDINATES);
				emit_uint8_t(&p, x);
				emit_uint8_t(&p, y);

				// Call Tile sublist
				emit_uint8_t(&p, GL_BRANCH_TO_SUBLIST);
				emit_uint32_t(&p, scene->tileDataBufferVC4 + (y * scene->binWth + x) * 32);

				// Last tile needs a special store instruction
				if (x == (scene->binWth - 1) && (y == scene->binHt - 1)) {
					// Store resolved tile color buffer and signal end of frame
					emit_uint8_t(&p, GL_STORE_MULTISAMPLE_END);
				}
				else {
					// Store resolved tile color buffer
					emit_uint8_t(&p, GL_STORE_MULTISAMPLE);
				}
			}
		}

		scene->loadpos = scene->renderControlVC4 + (p - q);			// Adjust VC4 load poistion
		scene->renderControlEndVC4 = scene->loadpos;				// Hold end of render control data

		return true;
	}
	return false;
}

bool v3d_SetupBinningConfig (RENDER_STRUCT* scene) {
if (scene)
	{
		uint8_t *p = (uint8_t*)(uintptr_t)GPUaddrToARMaddr(scene->binningDataVC4); // ARM address for binning data load
		uint8_t *list = p;												// Hold start address

		emit_uint8_t(&p, GL_TILE_BINNING_CONFIG);						// tile binning config control 
		emit_uint32_t(&p, scene->tileDataBufferVC4);					// tile allocation memory address
		emit_uint32_t(&p, scene->tileMemSize);							// tile allocation memory size
		emit_uint32_t(&p, scene->tileStateDataVC4);						// Tile state data address
		emit_uint8_t(&p, scene->binWth);								// renderWidth/64
		emit_uint8_t(&p, scene->binHt);									// renderHt/64
		emit_uint8_t(&p, 0x04);											// config

		// Start tile binning.
		emit_uint8_t(&p, GL_START_TILE_BINNING);						// Start binning command

		// Primitive type
		emit_uint8_t(&p, GL_PRIMITIVE_LIST_FORMAT);
		emit_uint8_t(&p, 0x32);											// 16 bit triangle

		// Clip Window
		emit_uint8_t(&p, GL_CLIP_WINDOW);								// Clip window 
		emit_uint16_t(&p, 0);											// 0
		emit_uint16_t(&p, 0);											// 0
		emit_uint16_t(&p, scene->renderWth);							// width
		emit_uint16_t(&p, scene->renderHt);								// height

		// GL State
		emit_uint8_t(&p, GL_CONFIG_STATE);
		emit_uint8_t(&p, 0x03);											// enable both foward and back facing polygons
		emit_uint8_t(&p, 0x00);											// depth testing disabled
		emit_uint8_t(&p, 0x02);											// enable early depth write

		// Viewport offset
		emit_uint8_t(&p, GL_VIEWPORT_OFFSET);							// Viewport offset
		emit_uint16_t(&p, 0);											// 0
		emit_uint16_t(&p, 0);											// 0

		// The triangle
		// No Vertex Shader state (takes pre-transformed vertexes so we don't have to supply a working coordinate shader.)
		emit_uint8_t(&p, GL_NV_SHADER_STATE);
		emit_uint32_t(&p, scene->fragShaderRecStart);					// Shader Record

		// primitive index list
		emit_uint8_t(&p, GL_INDEXED_PRIMITIVE_LIST);					// Indexed primitive list command
		emit_uint8_t(&p, PRIM_TRIANGLE);								// 8bit index, triangles
		emit_uint32_t(&p, scene->IndexVertexCt);						// Number of index vertex
		emit_uint32_t(&p, scene->indexVertexVC4);						// Address of index vertex data
		emit_uint32_t(&p, scene->MaxIndexVertex);						// Maximum index


		// End of bin list
		// So Flush
		emit_uint8_t(&p, GL_FLUSH_ALL_STATE);
		// Nop
		emit_uint8_t(&p, GL_NOP);
		// Halt
		emit_uint8_t(&p, GL_HALT);
		scene->binningCfgEnd = scene->binningDataVC4 + (p-list);		// Hold binning data end address

		return true;
	}
	return false;
}

void v3d_RenderScene (RENDER_STRUCT* scene) {
if (scene) {
		// clear caches
		v3d[V3D_L2CACTL] = 4;
		v3d[V3D_SLCACTL] = 0x0F0F0F0F;

		// stop the thread
		v3d[V3D_CT0CS] = 0x20;
		// wait for it to stop
		while (v3d[V3D_CT0CS] & 0x20);

		// Run our control list
		v3d[V3D_BFC] = 1;											// reset binning frame count
		v3d[V3D_CT0CA] = scene->binningDataVC4;						// Start binning config address
		v3d[V3D_CT0EA] = scene->binningCfgEnd;						// End binning config address is at render control start

        printf("\n wait for binning to finish \n");
		// wait for binning to finish
		while (v3d[V3D_BFC] == 0) {
			printf("V3D_CT0CA: 0x%x, V3D_PCS: 0x%x V3D_ERRSTAT: 0x%x \n", v3d[V3D_CT0CA], v3d[V3D_PCS], v3d[V3D_ERRSTAT]);
		}
         printf("\n wait for binning to finish completeed \n");
		// stop the thread
		v3d[V3D_CT1CS] = 0x20;
           printf("\n stopped thread \n");
		// Wait for thread to stop
		while (v3d[V3D_CT1CS] & 0x20);
        printf("\n wait complete for  stopped thread \n");
		// Run our render
		v3d[V3D_RFC] = 1;											// reset rendering frame count
		v3d[V3D_CT1CA] = scene->renderControlVC4;					// Start address for render control
		v3d[V3D_CT1EA] = scene->renderControlEndVC4;				// End address for render control
        printf("\n wait render \n");

		// wait for render to finish
		while (v3d[V3D_RFC] == 0) {}
        printf("\n wait render completed\n");

	}
}