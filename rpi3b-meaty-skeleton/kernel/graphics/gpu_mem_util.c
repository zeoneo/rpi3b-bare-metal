#include <graphics/gpu_mem_util.h>

struct __attribute__((__packed__, aligned(1))) EMITDATA
{
	uint8_t byte1;
	uint8_t byte2;
	uint8_t byte3;
	uint8_t byte4;
};

void emit_uint8_t(uint8_t **list, uint8_t d)
{
	*((*list)++) = d;
}

void emit_uint16_t(uint8_t **list, uint16_t d)
{
	struct EMITDATA *data = (struct EMITDATA *)&d;
	*((*list)++) = (*data).byte1;
	*((*list)++) = (*data).byte2;
}

void emit_uint32_t(uint8_t **list, uint32_t d)
{
	struct EMITDATA *data = (struct EMITDATA *)&d;
	*((*list)++) = (*data).byte1;
	*((*list)++) = (*data).byte2;
	*((*list)++) = (*data).byte3;
	*((*list)++) = (*data).byte4;
}

void emit_float(uint8_t **list, float f)
{
	struct EMITDATA *data = (struct EMITDATA *)&f;
	*((*list)++) = (*data).byte1;
	*((*list)++) = (*data).byte2;
	*((*list)++) = (*data).byte3;
	*((*list)++) = (*data).byte4;
}