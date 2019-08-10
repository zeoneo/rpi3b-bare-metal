#ifndef _ROM_FS_NEW_
#define _ROM_FS_NEW_

#ifdef __cplusplus
extern "C"
{
#endif

#include<stdint.h>

typedef struct {
    uint32_t inode;
	uint32_t file_size;
    uint8_t *file_base_ptr;
} file_info_t;

void read_file_content(file_info_t file_info, uint32_t offset, uint32_t size, uint8_t *buffer);
void get_inode_for(char *file_path, file_info_t *file_info);
uint32_t get_file_size_for(uint32_t inode);
uint32_t read_file_for(uint32_t inode, uint8_t *buffer, uint32_t file_size);
void print_file_x(uint32_t inode, uint32_t data_offset, uint32_t file_size);

#ifdef __cplusplus
}
#endif

#endif