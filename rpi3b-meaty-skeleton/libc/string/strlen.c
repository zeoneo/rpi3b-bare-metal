#include <string.h>

size_t strlen(const void* str) {
	size_t len = 0;
	const uint8_t * s = (uint8_t *)str;
	while (s[len])
		len++;
	return len;
}
