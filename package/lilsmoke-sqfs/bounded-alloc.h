/* Applied only to this private libsquashfs build, never to libc/liblzma. */
#ifndef LILSMOKE_SQFS_BOUNDED_ALLOC_H
#define LILSMOKE_SQFS_BOUNDED_ALLOC_H
#include <stdlib.h>
#include <stdint.h>
#include <errno.h>
#define OD_SQFS_MAX_ALLOC (1024U * 1024U)
static inline void *od_sqfs_malloc(size_t n)
{
	if (n > OD_SQFS_MAX_ALLOC) { errno = ENOMEM; return NULL; }
	return malloc(n);
}
static inline void *od_sqfs_calloc(size_t n, size_t s)
{
	if (s && n > OD_SQFS_MAX_ALLOC / s) { errno = ENOMEM; return NULL; }
	return calloc(n, s);
}
static inline void *od_sqfs_realloc(void *p, size_t n)
{
	if (n > OD_SQFS_MAX_ALLOC) { errno = ENOMEM; return NULL; }
	return realloc(p, n);
}
#define malloc od_sqfs_malloc
#define calloc od_sqfs_calloc
#define realloc od_sqfs_realloc
#endif
