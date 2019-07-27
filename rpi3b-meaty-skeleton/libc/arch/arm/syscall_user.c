#include <stddef.h>
#include <stdint.h>

#include <arch/arm/syscall_user.h>

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

/* 1 */
int32_t exit(int32_t status) {

	register long r7 __asm__("r7") = __NR_exit;
	register long r0 __asm__("r0") = status;

	__asm__ volatile(
		"svc #0\n"
		: "=r"(r0)
		: "r"(r7), "0"(r0)
		: "memory");

	return r0;
}

/* 3 */
int32_t read(int fd, void *buf, size_t count) {

	register long r7 __asm__("r7") = __NR_read;
	register long r0 __asm__("r0") = fd;
	register long r1 __asm__("r1") = (long)buf;
	register long r2 __asm__("r2") = count;

	__asm__ volatile(
		"svc #0\n"
		: "=r"(r0)
		: "r"(r7), "0"(r0), "r"(r1), "r"(r2)
		: "memory");

	return r0;
}


int32_t write(int fd, const void *buf, uint32_t size) {

	register long r7 __asm__("r7") = __NR_write;
	register long r0 __asm__("r0") = fd;
	register long r1 __asm__("r1") = (long)buf;
	register long r2 __asm__("r2") = size;

	__asm__ volatile(
		"svc #0\n"
		: "=r"(r0)
		: "r"(r7), "0"(r0), "r"(r1), "r"(r2)
		: "memory");

	return r0;
}

int32_t open(const char *filename, uint32_t flags, uint32_t mode) {

	register long r7 __asm__("r7") = __NR_open;
	register long r0 __asm__("r0") = (long)filename;
	register long r1 __asm__("r1") = flags;
	register long r2 __asm__("r2") = mode;

	__asm__ volatile(
		"svc #0\n"
		: "=r"(r0)
		: "r"(r7), "0"(r0), "r"(r1), "r"(r2)
		: "memory");

	return r0;
}

int32_t close(uint32_t fd) {

	register long r7 __asm__("r7") = __NR_close;
	register long r0 __asm__("r0") = fd;

	__asm__ volatile(
		"svc #0\n"
		: "=r"(r0)
		: "r"(r7), "0"(r0)
		: "memory");

	return r0;
}

int32_t execve(const char *filename, char *const argv[],
		char *const envp[]) {

	register long r7 __asm__("r7") = __NR_execve;
	register long r0 __asm__("r0") = (long)filename;
	register long r1 __asm__("r1") = (long)argv;
	register long r2 __asm__("r2") = (long)envp;


	__asm__ volatile(
		"svc #0\n"
		: "=r"(r0)
		: "r"(r7), "0"(r0), "r"(r1), "r"(r2)
		: "memory");

	return r0;

}

int32_t vfork(void) {

	register long r7 __asm__("r7") = __NR_vfork;
	register long r0 __asm__("r0");

	__asm__ volatile(
		"svc #0\n"
		: "=r"(r0) /* output */
		: "r"(r7) /* input */
		: "memory");

	return r0;

}

/* 345 */
int getcpu(uint32_t *cpu, uint32_t *node, void *tcache) {

	register long r7 __asm__("r7") = __NR_getcpu;
	register long r0 __asm__("r0") = (long)cpu;
	register long r1 __asm__("r1") = (long)node;
	register long r2 __asm__("r2") = (long)tcache;

	__asm__ volatile(
		"svc #0\n"
		: "=r"(r0)
		: "r"(r7), "0"(r0), "r"(r1), "r"(r2)
		: "memory");

	return r0;
}
