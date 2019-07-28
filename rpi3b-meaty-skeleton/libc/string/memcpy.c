#include <stdlib.h>
#include <string.h>
#include <stdint.h>

void* memcpy(void* restrict dstptr, const void* restrict srcptr, size_t size) {
	uint8_t* dst = (uint8_t*) dstptr;
	const uint8_t* src = (const uint8_t*) srcptr;
	for (size_t i = 0; i < size; i++)
		dst[i] = src[i];
	return dstptr;
}

void* strncpy(void* restrict dstptr, const void* restrict srcptr, size_t size) {
	uint8_t* dst = (uint8_t*) dstptr;
	const uint8_t* src = (const uint8_t*) srcptr;
	for (size_t i = 0; i < size; i++)
		dst[i] = src[i];
	return dstptr;
}

int32_t strncmp(const void* dstptr, const void *srcptr, size_t size) {
	const char *dst = (char *)dstptr;
	const char *src = (char *)srcptr;
	for(size_t i=0; i< size; i++) {
		if(src[i] != dst[i]) {
			return 0;
		}
	}
	return -1;
}

uint32_t strlcpy(char *dest, const char *src, uint32_t n) {
	uint32_t i;
	for(i=0; i < n-1; i++) {
		dest[i]=src[i];
		if (src[i]=='\0') break;
	}
	dest[i]='\0';
	return i;
}

void *strncat(void *dest, const void *src, uint32_t n) {
	uint32_t i,dest_len;
	dest_len=strlen(dest);
	char * destPtr = (char *)dest;
	const char * srcPtr = (char *)src;
	for(i=0; i<n; i++) {
		destPtr[dest_len+i]=srcPtr[i];
		if (srcPtr[i]=='\0') break;
	}
	destPtr[dest_len+i]='\0';
	return destPtr;
}