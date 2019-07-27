#ifndef _ROM_FS_H
#define _ROM_FS_H

#ifdef __cplusplus
extern "C"
{
#endif

#include<stdint.h>
#include<fs/files.h>

struct romfs_header_t {
	uint8_t magic[8];
	uint32_t size;
	uint32_t checksum;
	uint8_t volume_name[17];
	uint32_t first_offset;
};

struct romfs_file_header_t {
	uint32_t addr;
	uint32_t next;
	uint32_t type;
	uint32_t special;
	uint32_t size;
	uint32_t checksum;
	uint32_t filename_start;
	uint32_t data_start;
};

int32_t open_romfs_file(uint8_t *name,
		struct romfs_file_header_t *file);
int32_t romfs_get_inode(int32_t dir_inode, const uint8_t *name);
int32_t romfs_read_file(uint32_t inode, uint32_t offset,
			void *buf,uint32_t count);
int32_t romfs_mount(struct superblock_t *superblock);
int32_t romfs_stat(int32_t inode, struct stat *buf);
int32_t romfs_getdents(uint32_t dir_inode,
		uint32_t *current_inode, void *buf,uint32_t size);
int32_t romfs_statfs(struct superblock_t *superblock,struct statfs *buf);

#define ROMFS_TYPE_HARDLINK	0
#define ROMFS_TYPE_DIRECTORY	1
#define ROMFS_TYPE_REGULARFILE	2
#define ROMFS_TYPE_SYMBOLICLINK	3
#define ROMFS_TYPE_BLOCKDEV	4
#define ROMFS_TYPE_CHARDEV	5
#define ROMFS_TYPE_SOCKET	6
#define ROMFS_TYPE_FIFO		7

#ifdef __cplusplus
}
#endif

#endif