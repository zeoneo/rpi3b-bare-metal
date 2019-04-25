#include <string.h>
#include <fs/fat.h>
#include <device/emmc.h>
#include <plibc/stdio.h>

struct __attribute__((packed, aligned(2))) partition_info
{
    uint8_t status;        // 0x80 - active partition
    uint8_t headStart;     // starting head
    uint16_t cylSectStart; // starting cylinder and sector
    uint8_t type;          // partition type (01h = 12bit FAT, 04h = 16bit FAT, 05h = Ex MSDOS, 06h = 16bit FAT (>32Mb), 0Bh = 32bit FAT (<2048GB))
    uint8_t headEnd;       // ending head of the partition
    uint16_t cylSectEnd;   // ending cylinder and sector
    uint32_t firstSector;  // total sectors between MBR & the first sector of the partition
    uint32_t sectorsTotal; // size of this partition in sectors
} __packed;

struct __attribute__((packed, aligned(4))) mbr
{
    uint8_t nothing[446];                   // Filler the gap in the structure
    struct partition_info partitionData[4]; // partition records (16x4)
    uint16_t signature;                     // 0xaa55
};

struct __attribute__((packed, aligned(4))) sd_partition
{
    uint32_t rootCluster;         // Active partition rootCluster
    uint32_t sectorPerCluster;    // Active partition sectors per cluster
    uint32_t bytesPerSector;      // Active partition bytes per sector
    uint32_t firstDataSector;     // Active partition first data sector
    uint32_t dataSectors;         // Active partition data sectors
    uint32_t unusedSectors;       // Active partition unused sectors
    uint32_t reservedSectorCount; // Active partition reserved sectors,
    uint32_t fatSize;
};


typedef enum {
    ATTR_FILE_EMPTY = 0x00,
    ATTR_READ_ONLY = 0x01,
    ATTR_HIDDEN = 0x02,
    ATTR_SYSTEM = 0x04,
    ATTR_FILE_LABEL = 0x8,
    ATTR_DIRECTORY = 0x10,
    ATTR_ARCHIVE = 0x20,
    ATTR_DEVICE = 0x40,
    ATTR_NORMAL = 0x80,
    ATTR_FILE_DELETED = 0xE5,
    ATTR_LONG_NAME = ATTR_READ_ONLY | ATTR_HIDDEN | ATTR_SYSTEM | ATTR_FILE_LABEL
} FILE_ATTRIBUTE;

static fat_type selected_fat_type = FAT_NULL;
static struct sd_partition current_sd_partition = {0};

struct __attribute__((packed, aligned(1))) dir_Structure {
	sfn_name_t	name;						// 8.3 filename
	uint8_t		attrib;						// file attributes
	uint8_t		NTreserved;					// always 0
	uint8_t		timeTenth;					// tenths of seconds, set to 0 here
	uint16_t	writeTime;					// time file was last written
	uint16_t	writeDate;					// date file was last written
	uint16_t	lastAccessDate;				// date file was last accessed
	uint16_t	firstClusterHI;				// higher word of the first cluster number
	uint16_t	createTime;					// time file was created
	uint16_t	createDate;					// date file was created
	uint16_t	firstClusterLO;				// lower word of the first cluster number
	uint32_t	fileSize;					// size of file in bytes
};


void getFatEntrySectorAndOffset(uint32_t cluster_number, uint32_t * fat_sector_num, uint32_t * fat_entry_offset);
uint32_t getFirstSectorOfCluster(uint32_t cluster_number);


bool initialize_fat()
{
    if (sdInitCard(false) != SD_OK)
    {
        printf("Failed to initialize SD card");
        return false;
    }
    uint8_t num_of_blocks = 1;

    uint8_t bpb_buffer[512] __attribute__((aligned(4)));
    if (!sdcard_read(0, num_of_blocks, (uint8_t *)&bpb_buffer[0]))
    {
        printf("FAT: UNABLE to read first block in sd card. \n");
        return false;
    }

    struct fat_bpb *bpb = (struct fat_bpb *)bpb_buffer;
    if (bpb->bootjmp[0] != 0xE9 && bpb->bootjmp[0] != 0xEB)
    {
        struct mbr *mbr_info = (struct mbr *)&bpb_buffer[0];
        if (mbr_info->signature != 0xaa55)
        {
            printf("BPP is not boot sector or MBR. Corrupted data ???. \n");
            return false;
        }
        else
        {
            printf("Got MBR.. \n");
            struct partition_info *pd = &mbr_info->partitionData[0];
            current_sd_partition.unusedSectors = pd->firstSector;
            // Read first unused sector i.e. 512 Bytes
            if (!sdcard_read(pd->firstSector, 1, (uint8_t *)&bpb_buffer[0]))
            {
                printf("Could not mbr first sector. \n");
                return false;
            }
            // No need to map bpb_buffer to fat_bpb struct again, but this is easy to understand
            // struct fat_bpb *mbr_bpb = (struct fat_bpb *)bpb_buffer;
            if (bpb->bootjmp[0] != 0xE9 && bpb->bootjmp[0] != 0xEB)
            {
                printf("MBR doesn't have valid FAT_BPB sector. \n");
                return false;
            }
            else
            {
                printf("Got valid FAT_BPB for given MBR. \n");
            }
        }
    }
    else
    {
        printf("Got BOOT SECTOR.. \n");
    }

    current_sd_partition.bytesPerSector = bpb->bytes_per_sector;      // Bytes per sector on partition
    current_sd_partition.sectorPerCluster = bpb->sectors_per_cluster; // Hold the sector per cluster count
    current_sd_partition.reservedSectorCount = bpb->reserved_sector_count;

    if ((bpb->fat16_table_size == 0) && (bpb->root_entry_count == 0))
    {
        selected_fat_type = FAT32;
        printf("Got FAT 32 FS. \n");

        current_sd_partition.rootCluster = bpb->fat_type_data.ex_fat32.root_cluster; // Hold partition root cluster
        current_sd_partition.firstDataSector = bpb->reserved_sector_count + bpb->hidden_sector_count + (bpb->fat_type_data.ex_fat32.fat_table_size_32 * bpb->fat_table_count);
        current_sd_partition.dataSectors = bpb->fat32_total_sectors - bpb->reserved_sector_count - (bpb->fat_type_data.ex_fat32.fat_table_size_32 * bpb->fat_table_count);
        current_sd_partition.fatSize = bpb->fat_type_data.ex_fat32.fat_table_size_32;
        printf("ex_fat32 Volume Label: %s \n", bpb->fat_type_data.ex_fat32.volume_label); // Basic detail print if requested
    }
    else
    {
        selected_fat_type = FAT16;
        printf("Got FAT 16 FS. \n");

        current_sd_partition.fatSize = bpb->fat16_table_size;
        current_sd_partition.rootCluster = 2; // Hold partition root cluster, FAT16 always start at 2
        current_sd_partition.firstDataSector = current_sd_partition.unusedSectors + (bpb->fat_table_count * bpb->fat16_table_size) + 1;
        // data sectors x sectorsize = capacity ... I have check this on PC and gives right calc
        current_sd_partition.dataSectors = bpb->fat32_total_sectors - (bpb->fat_table_count * bpb->fat16_table_size) - 33; // -1 see above +1 and 32 fixed sectors
        if (bpb->fat_type_data.ex_fat1612.boot_signature == 0x29)
        {
            printf("FAT12/16 Volume Label: %s\n", bpb->fat_type_data.ex_fat1612.volume_label); // Basic detail print if requested
        }
    }

    uint32_t partition_totalClusters = current_sd_partition.dataSectors / current_sd_partition.sectorPerCluster;
    printf("First Sector: %lu, Data Sectors: %lu, TotalClusters: %lu, RootCluster: %lu\n",
           (unsigned long)current_sd_partition.firstDataSector, (unsigned long)current_sd_partition.dataSectors,
           (unsigned long)partition_totalClusters, (unsigned long)current_sd_partition.rootCluster);

    return true;
}

void print_root_directory_info()
{
    uint32_t root_directory_cluster_number = current_sd_partition.rootCluster;
    uint32_t fat_sector_num, fat_entry_offset;
    uint32_t first_root_dir_sector = getFirstSectorOfCluster(root_directory_cluster_number);
    getFatEntrySectorAndOffset(root_directory_cluster_number, &fat_sector_num, &fat_entry_offset);


    printf("FAT sector for root directory : %d \n", fat_sector_num);
    printf("FAT entry offset of root directory : %d \n", fat_entry_offset);
    printf("First Sector of Root Directory : %d", first_root_dir_sector);

    uint8_t buffer[512] __attribute__((aligned(4)));
    if (!sdcard_read(first_root_dir_sector, 1, (uint8_t *)&buffer[0]))
    {
        printf("FAT: ROOT DIR sector :%d. \n", first_root_dir_sector);
        return;
    }

    uint32_t limit = 512;
    uint32_t index = 0;
    printf("limit: %d \n", limit);
    while(index < limit) {
        struct dir_sfn_entry * dir_entry = (struct dir_sfn_entry *) &buffer[index];
        // if(dir_entry->short_file_name[0] != ATTR_FILE_EMPTY || dir_entry->short_file_name[0] != ATTR_FILE_DELETED) {
            if(dir_entry->file_attrib == ATTR_FILE_LABEL) {
                // printf("dir_entry->short_file_name : %s \n", dir_entry->short_file_name);

                printf("LABEL: %c%c%c%c%c%c%c%c%c%c%c \n",
							dir_entry->short_file_name[0], dir_entry->short_file_name[1], dir_entry->short_file_name[2], dir_entry->short_file_name[3],
							dir_entry->short_file_name[4], dir_entry->short_file_name[5], dir_entry->short_file_name[6], dir_entry->short_file_name[7],
							dir_entry->short_file_name[8], dir_entry->short_file_name[9], dir_entry->short_file_name[10]);
            } else if (dir_entry->file_attrib == ATTR_DIRECTORY) {
                printf("Got directory here. \n");
            } else if (dir_entry->file_attrib == ATTR_LONG_NAME) {
                printf("Got ATTR_LONG_NAME here. \n");
            }
        // }
        printf("dir_entry->file_attrib :%x \n", dir_entry->file_attrib);
        index += sizeof(struct dir_sfn_entry);
        printf("index: %d \n", index);
    }

    //Try reading FAT sector
    if (!sdcard_read(2, 1, (uint8_t *)&buffer[0]))
    {
        printf("FAT: Could not read FAT sector :%d. \n", fat_sector_num);
        return;
    }

    uint32_t * fat_cluster_entry = (uint32_t *)&buffer[0];
    fat_cluster_entry = fat_cluster_entry + fat_entry_offset;

    printf("Content of fat cluster entry : %x \n" , *fat_cluster_entry);
    uint32_t x1 =0;
    while(x1 < 1) {
        sdcard_read(x1, 1, (uint8_t *)&buffer[0]);
        printf("\n------------DUMP of SECTOR: %d -----------\n", x1);
        uint16_t i = 0;
        while(i < 512) printf("%x ", buffer[i++]);

        x1++;
    }

}


void getFatEntrySectorAndOffset(uint32_t cluster_number, uint32_t * fat_sector_num, uint32_t * fat_entry_offset) {
    uint32_t fat_offset = cluster_number * 4; //assuming its fat32;
    if(selected_fat_type == FAT16) {
        fat_offset = cluster_number * 2;
    }

    *fat_sector_num = current_sd_partition.reservedSectorCount + (fat_offset / current_sd_partition.bytesPerSector); 
    *fat_entry_offset = (fat_offset % current_sd_partition.bytesPerSector);
}

uint32_t getFirstSectorOfCluster(uint32_t cluster_number) {
    uint32_t firstSector = (cluster_number - 2) * current_sd_partition.sectorPerCluster;
    firstSector = firstSector + current_sd_partition.firstDataSector;
    return firstSector;
}
