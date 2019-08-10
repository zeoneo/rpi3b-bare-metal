#ifndef _ELF_INTERFACE_LAYER_
#define _ELF_INTERFACE_LAYER_

#ifdef __cplusplus
extern "C"
{
#endif

#include<stdio.h>

#define LOADER_FD_T FILE *
#define LOADER_OPEN_FOR_RD(path) fopen(path, "rb")
#define LOADER_FD_VALID(fd) (fd != NULL)
#define LOADER_READ(fd, buffer, size) fread(buffer, 1, size, fd)
#define LOADER_WRITE(fd, buffer, size) fwrite(buffer, 1, size, fd)
#define LOADER_CLOSE(fd) fclose(fd)
#define LOADER_SEEK_FROM_START(fd, off) fseek(fd, off, SEEK_SET)
#define LOADER_TELL(fd) ftell(fd)

#ifdef __cplusplus
}
#endif

#endif


