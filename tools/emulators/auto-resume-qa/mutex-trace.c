/* SPDX-License-Identifier: MPL-2.0 */
/* QA observer only: never suppress, replace or repair a mutex operation. */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/syscall.h>
#include <unistd.h>

int pthread_mutex_destroy(pthread_mutex_t *mutex)
{
    static int (*original)(pthread_mutex_t *);
    uint32_t words[5];
    Dl_info caller = {0};
    char message[512];
    void *ra = __builtin_return_address(0);
    if (!original) original = (int (*)(pthread_mutex_t *))dlsym(RTLD_NEXT, "pthread_mutex_destroy");
    if (!original) _exit(94);
    /* musl/MIPS words: type, lock(owner), waiters, previous, next. */
    memcpy(words, mutex, sizeof(words));
    dladdr(ra, &caller);
    int result = original(mutex);
    int count = snprintf(message, sizeof(message),
        "QA_MUTEX_DESTROY tid=%ld mutex=%p type=%08x owner=%08x wait=%08x prev=%08x next=%08x result=%d caller=%s+0x%lx\n",
        syscall(SYS_gettid), (void *)mutex, words[0], words[1], words[2], words[3], words[4], result,
        caller.dli_fname ? caller.dli_fname : "unknown", (unsigned long)((uintptr_t)ra - (uintptr_t)caller.dli_fbase));
    if (count > 0) (void)write(STDERR_FILENO, message, (size_t)count < sizeof(message) ? (size_t)count : sizeof(message) - 1);
    return result;
}
