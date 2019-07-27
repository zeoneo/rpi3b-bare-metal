#include <stddef.h>
#include <stdint.h>

#define __NR_exit	1
#define __NR_read	3
#define __NR_write	4
#define __NR_open	5
#define __NR_close	6
#define __NR_waitpid	7
#define __NR_execve	11
#define __NR_chdir	12
#define __NR_time	13
#define __NR_getpid	20
#define __NR_times	43
#define __NR_ioctl	54
#define __NR_reboot	88
#define __NR_mmap	90
#define __NR_munmap	91
#define __NR_statfs	99
#define __NR_stat	106
#define __NR_sysinfo	116
#define	__NR_uname	122
#define __NR_getdents	141
#define __NR_nanosleep  162
#define __NR_getcwd	183
#define __NR_vfork	190
#define __NR_clock_gettime	263
#define __NR_statfs64	266
#define __NR_getcpu	345

int32_t exit(int32_t status);
int32_t read(int fd, void *buf, size_t count);
int32_t write(int fd, const void *buf, uint32_t size);
int32_t open(const char *filename, uint32_t flags, uint32_t mode);
int32_t close(uint32_t fd);
int32_t execve(const char *filename, char *const argv[],
		char *const envp[]);
int32_t vfork(void);
int getcpu(uint32_t *cpu, uint32_t *node, void *tcache);