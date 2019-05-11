#ifndef _OPEN_GL_ES_H_
#define _OPEN_GL_ES_H_

#ifdef __cplusplus
extern "C"
{
#endif

#include<stdint.h>

#define GL_FRAGMENT_SHADER	35632
#define GL_VERTEX_SHADER	35633

// typedef int (*printhandler) (const char *fmt, ...);

// int32_t load_shader(int32_t shaderType);

void do_rotate(float delta);
// Render a single triangle to memory.
void test_triangle (uint16_t renderWth, uint16_t renderHt, uint32_t renderBufferAddr);


#ifdef __cplusplus
}
#endif

#endif