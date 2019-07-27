
struct file_struct {
	int fd;
};
typedef struct file_struct FILE;
FILE stdin,stdout,stderr;
int printf(const char *string,...);
