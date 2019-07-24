#ifndef _KERNEL_LOCKS_H
#define _KERNEL_LOCKS_H

#ifdef __cplusplus
extern "C"
{
#endif

#define MUTEX_LOCKED	1
#define MUTEX_UNLOCKED	0

extern void mutex_lock(void *mutex);
extern void mutex_unlock (void *mutex);

#ifdef __cplusplus
}
#endif

#endif