/* SurfaceRT_OS - errno values (negative on error) */
#ifndef SRTOS_ERRNO_H
#define SRTOS_ERRNO_H

#define EPERM 1
#define ENOENT 2
#define EIO 5
#define ENXIO 6
#define EAGAIN 11
#define ENOMEM 12
#define EACCES 13
#define EBUSY 16
#define EEXIST 17
#define ENODEV 19
#define EINVAL 22
#define ENOSPC 28
#define ERANGE 34
#define ENOSYS 38
#define ENOTSUP 95
#define ETIMEDOUT 110
#define ENOBUFS 105

#define IS_ERR(p) ((sptr)(p) < 0 && (sptr)(p) > -4096)

#endif
