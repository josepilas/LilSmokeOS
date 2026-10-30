#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/file.h>

int flock(int fd, int operation) { (void)fd; (void)operation; errno = ENOSYS; return -1; }
int fcntl(int fd, int command, ...) {
    if (command == F_SETLK || command == F_SETLKW || command == F_GETLK) { errno = ENOSYS; return -1; }
    int (*real)(int, int, ...) = dlsym(RTLD_NEXT, "fcntl");
    if (!real) { errno = ENOSYS; return -1; }
    if (command == F_GETFD || command == F_GETFL || command == F_GETOWN) return real(fd, command);
    va_list args; va_start(args, command);
    int result;
    if (command == F_SETFD || command == F_SETFL || command == F_SETOWN || command == F_DUPFD || command == F_DUPFD_CLOEXEC)
        result = real(fd, command, va_arg(args, int));
    else result = real(fd, command, va_arg(args, void *));
    va_end(args); return result;
}
__attribute__((constructor)) static void verify_denial(void) {
    struct flock lock = { .l_type = F_WRLCK, .l_whence = SEEK_SET };
    if (flock(-1, LOCK_EX) != -1 || errno != ENOSYS || fcntl(-1, F_SETLK, &lock) != -1 || errno != ENOSYS)
        abort();
    fprintf(stderr, "LOCK_QA flock=ENOSYS fcntl_setlk=ENOSYS errno=%d\n", ENOSYS);
}
