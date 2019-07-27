#include<stdlib.h>

void* memset(void* bufptr, uint8_t value, size_t size) {
	uint8_t * buf = (uint8_t*) bufptr;
	for (size_t i = 0; i < size; i++)
		buf[i] = (uint8_t) value;
	return bufptr;
}
