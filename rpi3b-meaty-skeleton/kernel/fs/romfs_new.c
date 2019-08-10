#include<fs/romfs_new.h>
#include<fs/ramdisk.h>
#include<klib/printk.h>
#include<string.h>
#include<stdlib.h>
#include<endian.h>

// TODO: Fix this hardcoding
static uint32_t root_dir_inode=  0x20;

// static char file_types[8][16] = {
//     "Hard Link",
//     "Directory",
//     "Regular File",
//     "Symbolik Link",
//     "Block Device",
//     "Char Device",
//     "Socket",
//     "Fifo"
// };

static uint32_t get_first_file_inode(uint32_t dir_inode) {
    uint32_t next_file_offset = 0;
    ramdisk_read(dir_inode + 4, 4, (uint8_t *)&next_file_offset);
    return ntohl(next_file_offset);
}

static uint32_t get_matching_file(uint32_t current_inode, 
char *file_name, uint32_t *data_offset, uint32_t *file_size) {
    uint32_t next_inode = current_inode;
    uint32_t file_size_=0;
    while(1) {
        uint8_t next_file_name[17] = { '\0' }; //TODO: Optimize this

        uint32_t next_file_offset = 0;
        uint8_t fname_index = 16;
        if(next_inode == 0 ) {
            return 0;
        }
        ramdisk_read(next_inode + fname_index, 17, (uint8_t *)&next_file_name[0]);

        file_size_=0;
        ramdisk_read(next_inode + 8, 4, (uint8_t *)&file_size_);
        *file_size = ntohl(file_size_);
        uint8_t length = strlen(&next_file_name[0]);
        if(strncmp(&next_file_name[0], file_name, length)) {
            length += 1; // accomodate \0;
            // printk("Name mached: %s inode: %x len: %d \n",next_file_name, next_inode, length);
            uint32_t x = length % 16;
            if(x != 0) {
                *data_offset = next_inode + 16 + (length - x) + 16;
            } else {
                *data_offset = next_inode + 16 + length;
            }
            return next_inode;
        }
        ramdisk_read(next_inode, 4, (uint8_t *)&next_file_offset);
        next_file_offset = (ntohl(next_file_offset));
        next_file_offset &= 0xFFFFFFF0;
        next_inode = next_file_offset;
    };
    return 0;
}

static uint8_t get_next_dir_name(uint8_t *absolute_file_name, uint8_t *dest)
{
    uint8_t count = 0;
    uint8_t *src_ptr = &absolute_file_name[0];
    while (*src_ptr != '\0' && *src_ptr != '/' && *src_ptr >= 0x23 && *src_ptr <= 0x7e )
    {
        dest[count++] = *src_ptr;
        src_ptr++;
    }
    
    return count;
}

void print_file_x(uint32_t inode, uint32_t data_offset, uint32_t file_size) {
    uint32_t index = inode;
    index  = 0;
    while(index < file_size) {
        uint8_t data[513] = { '\0' };
        ramdisk_read(data_offset + index, 512, data);
        printk("%s", data);
        index += 512;
    }
}

uint32_t get_inode_for(char *file_path) {
    printk("Reading inode for file: %s \n", file_path);
    uint32_t length = strlen(file_path);
    if(length == 0x0) {
        printk("Length: 0 \n");
        return 0;
    } else if (length == 0x01 && *file_path == '/') {
        printk("Root dir: 0 \n");
        return root_dir_inode;
    }
    if(file_path[0] != '/') {
        printk("Provide absolute file path");
        return 0;
    }

    uint32_t next_inode = root_dir_inode;
    uint8_t next_index = 1;
    uint8_t next_name_count = 0;
    uint32_t last_inode = 0;
    uint32_t data_offset = 0;
    uint32_t file_size = 0;
    while(1) {
        char next_file_name[256] = { '\0' }; //TODO: Optimize this
        next_name_count = get_next_dir_name((uint8_t *)&file_path[next_index],(uint8_t *) &next_file_name[0]);
        next_index += next_name_count + 1;
        if(next_name_count == 0) {
            printk("Data offset: %x file_size %d \n", data_offset, file_size);
            print_file_x(last_inode, data_offset, file_size);
            return last_inode;
        }
        uint32_t inode = get_matching_file(next_inode, next_file_name, &data_offset, &file_size);
        if(inode == 0) {
            // printk("-------------------Inode zero for: %s ", next_file_name);
            return 0;
        }
        last_inode = inode;
        next_inode = get_first_file_inode(inode);
    };
    return 0;
}

uint32_t get_file_size_for(uint32_t inode) {
    return inode;
}

uint32_t read_file_for(uint32_t inode, uint8_t *buffer, uint32_t file_size) {
    printk(" %d %x %d \n", inode, buffer, file_size);
    return 0;
}