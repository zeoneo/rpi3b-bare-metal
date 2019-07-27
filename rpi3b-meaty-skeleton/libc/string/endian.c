#include<endian.h>
#include<stdint.h>

uint32_t htonl(uint32_t x) {
	// __asm__("rev %0, %0" : "+r"(x));
	return x;
}

uint32_t ntohl(uint32_t x) {
	// __asm__("rev %0, %0" : "+r"(x));
	return x;
}
