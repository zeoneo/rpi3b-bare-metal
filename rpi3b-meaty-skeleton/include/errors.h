#ifndef _ERROR_H
#define _ERROR_H

#ifdef __cplusplus
extern "C"
{
#endif

#define ENOENT	2	/* File not found..... */
#define EBADF	9	/* Bad file descriptor */
#define ECHILD	10      /* No child process... */
#define ENOMEM	12	/* Not enough memory.. */
#define EBUSY	16      /* Resource busy ..... */
#define ENODEV	19	/* No such device..... */
#define ENOTDIR	20	/* Not a directory.... */
#define EINVAL	22	/* Invalid argument... */
#define ENFILE	23	/* Not enough fds..... */
#define ERANGE	34	/* Result out of range */
#define ENOSYS	38	/* No such system call */

#ifdef __cplusplus
}
#endif

#endif

