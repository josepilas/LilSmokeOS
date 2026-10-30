#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <unistd.h>
#include <sys/syscall.h>

int flock(int fd, int operation) {
    (void)fd; (void)operation;
    errno = ENOSYS;
    return -1;
}

int fcntl(int fd, int command, ...) {
    if (command == F_SETLK || command == F_SETLKW || command == F_GETLK) {
        errno = ENOSYS;
        return -1;
    }
    long argument = 0;
    if (command != F_GETFD && command != F_GETFL && command != F_GETOWN) {
        va_list arguments;
        va_start(arguments, command);
        argument = va_arg(arguments, long);
        va_end(arguments);
    }
    return syscall(SYS_fcntl, fd, command, argument);
}
