#include<endian.h>
#include<stdint.h>

uint32_t htonl(uint32_t x) {
	uint8_t *s = (uint8_t *)&x;
	return (uint32_t) (s[0] << 24 | s[1] << 16 | s[2] << 8 | s[3]);
}

uint32_t ntohl(uint32_t x) {
	uint8_t *s = (uint8_t *)&x;
	return (uint32_t) (s[0] << 24 | s[1] << 16 | s[2] << 8 | s[3]);
}
