/* SPDX-License-Identifier: AGPL-3.0-only */
#ifndef UNDERTALE_PREP_LOCK_H
#define UNDERTALE_PREP_LOCK_H
typedef struct { int fd; long pid; unsigned long long start; char boot[40],path[700]; } FtPrepLock;
int ft_prep_lock(FtPrepLock*, const char* cache);
void ft_prep_unlock(FtPrepLock*);
#endif
