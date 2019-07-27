#include<string.h>
#include<fs/ramdisk.h>
#include<klib/printk.h>

struct ramdisk_info_t {
	const uint8_t *start;
	uint32_t length;
	uint32_t flags;
} ramdisk_info;

uint32_t initialize_ramdisk(const uint8_t *initrd_base, uint32_t initrd_size) {
	printk("Initializing ramdisk of size %d at address %x\n", initrd_size,initrd_base);
	ramdisk_info.start = initrd_base;
	ramdisk_info.length = initrd_size;
	return 0;
}

int32_t ramdisk_read(uint32_t offset, uint32_t length, char *dest) {
	/* Make sure we are in range */
	if (offset+length>ramdisk_info.length) {
			printk("ramdisk: access out of range %d > %d\n",
				offset+length,ramdisk_info.length);
		return -1;
	}
	memcpy(dest,ramdisk_info.start+offset,length);
	return length;
}
