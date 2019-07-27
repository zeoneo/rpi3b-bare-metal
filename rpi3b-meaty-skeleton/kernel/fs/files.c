#include<klib/printk.h>
#include<fs/romfs.h>
#include<fs/files.h>
#include<errors.h>
#include<string.h>

static int32_t root_dir=0;

#define MAX_FILENAME_SIZE 256

#define MAX_FD	32

struct fd_info_t {
	uint32_t valid;
	uint32_t inode;
	uint32_t file_ptr;
} fd_table[MAX_FD];

int32_t fd_free(uint32_t fd) {
    printk(" fd/; %d", fd);
	return -EBADF;
}

int32_t fd_allocate(uint32_t inode) {

	int32_t fd;

	printk("Attempting to allocate fd for inode %x\n",inode);

	fd=0;
	while(1) {
		if (fd_table[fd].valid==0) {
			fd_table[fd].valid=1;
			fd_table[fd].inode=inode;
			fd_table[fd].file_ptr=0;
			break;
		}
		fd++;
		if (fd>=MAX_FD) {
			fd=-ENFILE;
			break;
		}
	}

	printk("### Allocated fd %d\n",fd);

	return fd;
}


/* Split a filename into the path part and the actual name part */
static const uint8_t *split_filename(const uint8_t *start_ptr, uint8_t *name,
			int len) {

	const uint8_t *ptr=start_ptr;
	uint8_t *out=name;
	int length=0;

	while(1) {
		if (*ptr==0) {
			*out=0;
			return NULL;
		}

		if (length>=(len-1)) {
			*out=0;
			return NULL;
		}

		if (*ptr=='/') {
			*out=0;
			ptr++;
			break;
		}
		*out=*ptr;
		ptr++;
		out++;
		length++;
	}
	return ptr;
}

int32_t get_inode(const uint8_t *pathname) {

	int32_t inode;
	uint8_t name[MAX_FILENAME_SIZE];
	const uint8_t *ptr=pathname;
	int32_t dir_inode;

	/* start at root directory */
	if (*ptr=='/') {
		dir_inode=root_dir;
		ptr++;
	}
	else {
        dir_inode = 0;
		// dir_inode=current_proc[get_cpu()]->current_dir;
	}

	if (*ptr==0) {
		return dir_inode;
	}

	while(1) {
		{
			printk("get_inode: about to split %s\n",ptr);
		}

		ptr=split_filename(ptr,name,MAX_FILENAME_SIZE);

		{
			printk("get_inode: di=%x path_part %s\n",
							dir_inode,name);
		}

		if (ptr==NULL) break;
		dir_inode=romfs_get_inode(dir_inode,name);
	}

	inode=romfs_get_inode(dir_inode,name);
	if (inode<0) {
		printk("get_inode: error opening %s\n",name);
	}

	return inode;
}

int32_t close(uint32_t fd) {

	int32_t result;

	result=fd_free(fd);

	return result;

}


int32_t open(const uint8_t *pathname, uint32_t flags, uint32_t mode) {

	int32_t result;
	int32_t inode;

	printk("### Trying to open %s %d %d \n",pathname, flags, mode);

	inode=get_inode(pathname);
	if (inode<0) {
		return -ENOENT;
	}

	result=fd_allocate(inode);
	if (result<0) {
		return result;
	}

	printk("### opened fd %d\n",result);

	return result;

}

int32_t read(uint32_t fd, void *buf, uint32_t count) {

	int32_t result;


	if (fd==0) {
		result= 0x0; //console_read(buf,count);
	}
	else if (fd>=MAX_FD) {
		return -ENFILE;
	}
	else if (fd_table[fd].valid==0) {
		printk("Attempting to read from unsupported fd %d\n",fd);
		result=-EBADF;
	}
	else {
		printk("Attempting to read %d bytes from fd %d into %x\n",count,fd,buf);

		result=romfs_read_file(fd_table[fd].inode,
					fd_table[fd].file_ptr,
					buf,count);
		if (result>0) {
			fd_table[fd].file_ptr+=result;
		}
	}
	return result;
}

int32_t write(uint32_t fd, void *buf, uint32_t count) {

	int32_t result;

	if (fd==2) {
		int i;
		uint8_t *string = (uint8_t *)buf;
		{
			printk("Writing %d bytes, %d\n",count,string[count-1]);
			for(i=0;i<count;i++) {
				printk("%x ",string[i]);
			}
			printk("\n");
		}
	}

	if ((fd==1) || (fd==2)) {
		result = 0x0; //console_write(buf, count);
	}
	else {
		printk("Attempting to write unsupported fd %d\n",fd);
		result=-EBADF;
	}
	return result;
}

int32_t stat(const uint8_t *pathname, struct stat *buf) {

	int32_t inode;
	int32_t result;

	{
		printk("### Trying to stat %s\n",pathname);
	}

	inode=get_inode(pathname);
	if (inode<0) {
		return -ENOENT;
	}

	result=romfs_stat(inode, buf);

	return result;
}

struct superblock_t superblock_table[8];

int32_t mount(const uint8_t *source, const uint8_t *target,
	const uint8_t *filesystemtype, uint32_t mountflags,
	const void *data) {

	int32_t result=0;
    printk(" source: %x target:%x mflags: %x, data: %x \n", source, target, mountflags, data);
	if (!strncmp(filesystemtype,(uint8_t *)"romfs",5)) {
		result=romfs_mount(&superblock_table[0]);
		if (result>=0) {
			root_dir=result;
			result=0;
		}
	}
	else {
		result=-ENODEV;
	}

	return result;
}


void fd_table_init(void) {
	int i;

	for(i=0;i<MAX_FD;i++) {
		fd_table[i].valid=0;
	}

	/* Special case 0/1/2 (stdin/stdout/stderr) */
	/* FIXME: actually hook them up to be proper fds */
	fd_table[0].valid=1;
	fd_table[1].valid=1;
	fd_table[2].valid=1;

	return;
}

int32_t getdents(uint32_t fd, struct vmwos_dirent *dirp, uint32_t count) {

	int result;

	if (fd>=MAX_FD) {
		return -ENFILE;
	}
	else if (fd_table[fd].valid==0) {
		printk("Attempting to getdents from unsupported fd %d\n",fd);
		result=-EBADF;
	}
	/* FIXME: check if it's a directory fd */
	else {
		{
		}

		result=	romfs_getdents(fd_table[fd].inode,
					&(fd_table[fd].file_ptr),
					dirp,count);
	}
	return result;

}

/* Change current working directory */
int32_t chdir(const uint8_t *path) {

	int32_t inode,result;

	struct stat buf;

	inode=get_inode(path);
	if (inode<0) {
		return -ENOENT;
	}

	result=romfs_stat(inode, &buf);
	if (result<0) {
		return result;
	}

	if ((buf.st_mode&S_IFMT)!=S_IFDIR) {
		return -ENOTDIR;
	}

    //TODO:
	// current_proc[get_cpu()]->current_dir=inode;

	return 0;
}


/* Get name of current working directory */
uint8_t *getcwd(uint8_t *buf, size_t size) {

	struct stat stat_buf;

	int32_t inode,result;
    //TODO
	inode= 0x0; // current_proc[get_cpu()]->current_dir;
    printk("%x \n", buf);
	result=romfs_stat(inode, &stat_buf);

	(void)result;

	strncpy(buf,"BROKEN",size);

	return buf;

}

int32_t statfs(const uint8_t *path, struct statfs *buf) {
	/* FIXME: lookup path */
    printk("path : %x", path);
	return romfs_statfs(&superblock_table[0],buf);
}
