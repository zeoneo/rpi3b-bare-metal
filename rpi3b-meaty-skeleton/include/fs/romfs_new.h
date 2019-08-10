
#include<stdint.h>

uint32_t get_inode_for(char *file_path);
uint32_t get_file_size_for(uint32_t inode);
uint32_t read_file_for(uint32_t inode, uint8_t *buffer, uint32_t file_size);
void print_file_x(uint32_t inode, uint32_t data_offset, uint32_t file_size);