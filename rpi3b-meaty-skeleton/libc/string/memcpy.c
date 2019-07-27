#include <string.h>
#include <stdint.h>

void* memcpy(void* restrict dstptr, const void* restrict srcptr, size_t size) {
	unsigned char* dst = (unsigned char*) dstptr;
	const unsigned char* src = (const unsigned char*) srcptr;
	for (size_t i = 0; i < size; i++)
		dst[i] = src[i];
	return dstptr;
}

void* strncpy(void* restrict dstptr, const void* restrict srcptr, size_t size) {
	unsigned char* dst = (unsigned char*) dstptr;
	const unsigned char* src = (const unsigned char*) srcptr;
	for (size_t i = 0; i < size; i++)
		dst[i] = src[i];
	return dstptr;
}

int32_t strncmp(const uint8_t* dstptr, const uint8_t *srcptr, size_t size) {
	for(size_t i=0; i< size; i++) {
		if(dstptr[i] != srcptr[i]) {
			return 0;
		}
	}
	return -1;
}

int32_t strlcpy(uint8_t *dest, const uint8_t *src, uint32_t n) {
	uint32_t i;
	for(i=0; i<n-1; i++) {
		dest[i]=src[i];
		if (src[i]=='\0') break;
	}
	dest[i]='\0';
	return i;
}

uint8_t *strncat(uint8_t *dest, const uint8_t *src, uint32_t n) {
	uint32_t i,dest_len;
	dest_len=strlen(dest);
	for(i=0; i<n; i++) {
		dest[dest_len+i]=src[i];
		if (src[i]=='\0') break;
	}
	dest[dest_len+i]='\0';
	return dest;
}