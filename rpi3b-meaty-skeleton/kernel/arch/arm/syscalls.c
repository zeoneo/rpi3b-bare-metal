/* Support files for GNU libc.  Files in the system namespace go here.
   Files in the C namespace (ie those that do not start with an
   underscore) go in .c.  */

#include <_ansi.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/fcntl.h>
#include <plibc/stdio.h>
#include <time.h>
#include <sys/time.h>
#include <sys/times.h>
#include <errno.h>
#include <reent.h>

void uart_putc(uint32_t c);

uint32_t heap_end = 0x0020000;
uint32_t prev_heap_end;

/* Forward prototypes.  */
int32_t_system _PARAMS((const char *));
int32_t_rename _PARAMS((const char *, const char *));
int32_tisatty _PARAMS((int));
clock_t _times _PARAMS((struct tms *));
int32_t_gettimeofday _PARAMS((struct timeval *, struct timezone *));
void _raise _PARAMS((void));
int32_t_unlink _PARAMS((void));
int32_t_link _PARAMS((void));
int32_t_stat _PARAMS((const char *, struct stat *));
int32_t_fstat _PARAMS((int, struct stat *));
caddr_t _sbrk _PARAMS((int));
int32_t_getpid _PARAMS((int));
int32_t_kill _PARAMS((int, int));
void _exit _PARAMS((int));
int32_t_close _PARAMS((int));
int32_t_open _PARAMS((const char *, int, ...));
int32_t_write _PARAMS((int, char *, int));
int32_t_lseek _PARAMS((int, int, int));
int32_t_read _PARAMS((int, char *, int));
void initialise_monitor_handles _PARAMS((void));

//static int
//remap_handle (int32_tfh)
//{
//return 0;
//}

void initialise_monitor_handles(void)
{
}

//static int
//get_errno (void)
//{
//return(0);
//}

//static int
//error (int32_tresult)
//{
//errno = get_errno ();
//return result;
//}

int32_t_read(int32_tfile,
             char *ptr,
             int32_tlen)
{
    return len;
}

int32_t_lseek(int32_tfile,
              int32_tptr,
              int32_tdir)
{
    return 0;
}

int32_t_write(int32_tfile,
              char *ptr,
              int32_tlen)
{
    int32_tr;
    for (r = 0; r < len; r++)
        uart_putc(ptr[r]);
    return len;
}

int32_t_open(const char *path,
             int32_tflags,
             ...)
{
    return 0;
}

int32_t_close(int32_tfile)
{
    return 0;
}

void _exit(int32_tn)
{
    while (1)
        ;
}

int32_t_kill(int32_tn, int32_tm)
{
    return (0);
}

int32_t_getpid(int32_tn)
{
    return 1;
    n = n;
}

caddr_t
    _sbrk(int32_tincr)
{
    prev_heap_end = heap_end;
    heap_end += incr;
    return (caddr_t)prev_heap_end;
}

int32_t_fstat(int32_tfile, struct stat *st)
{
    return 0;
}

int32_t_stat(const char *fname, struct stat *st)
{
    return 0;
}

int32_t_link(void)
{
    return -1;
}

int32_t_unlink(void)
{
    return -1;
}

void _raise(void)
{
    return;
}

int32_t_gettimeofday(struct timeval *tp, struct timezone *tzp)
{
    if (tp)
    {
        tp->tv_sec = 10;
        tp->tv_usec = 0;
    }
    if (tzp)
    {
        tzp->tz_minuteswest = 0;
        tzp->tz_dsttime = 0;
    }
    return 0;
}

clock_t
_times(struct tms *tp)
{
    clock_t timeval;

    timeval = 10;
    if (tp)
    {
        tp->tms_utime = timeval; /* user time */
        tp->tms_stime = 0;       /* system time */
        tp->tms_cutime = 0;      /* user time, children */
        tp->tms_cstime = 0;      /* system time, children */
    }
    return timeval;
};

int32_t_isatty(int32_tfd)
{
    return 1;
    fd = fd;
}

int32_t_system(const char *s)
{
    if (s == NULL)
        return 0;
    errno = ENOSYS;
    return -1;
}

int32_t_rename(const char *oldpath, const char *newpath)
{
    errno = ENOSYS;
    return -1;
}
