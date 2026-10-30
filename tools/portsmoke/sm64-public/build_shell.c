/* SPDX-License-Identifier: MPL-2.0 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <utime.h>

/* Make owns the command. This shell only timestamps its successful primary
 * output in the private build tree; it never changes the system clock. */
static int output_fd(const char *target) {
    char path[1024];
    const char prefix[] = "build/original-mips/";
    if (!target || !*target || strncmp(target, prefix, sizeof prefix - 1))
        return -2; /* make-time probes and non-build phony targets */
    if (strlen(target) >= sizeof path) { errno = ENAMETOOLONG; return -1; }
    strcpy(path, target);
    int fd = open(".", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (fd < 0) return -1;
    char *part = path;
    for (;;) {
        char *slash = strchr(part, '/');
        if (slash) *slash = '\0';
        if (!*part || !strcmp(part, ".") || !strcmp(part, "..")) {
            close(fd); errno = EINVAL; return -1;
        }
        int next = openat(fd, part, O_RDONLY | O_NOFOLLOW | O_CLOEXEC |
                          (slash ? O_DIRECTORY : O_NONBLOCK));
        int error = errno;
        close(fd);
        if (next < 0) { errno = error; return -1; }
        fd = next;
        if (!slash) break;
        part = slash + 1;
    }
    struct stat info;
    if (fstat(fd, &info) || (!S_ISREG(info.st_mode) && !S_ISDIR(info.st_mode)) ||
        (S_ISREG(info.st_mode) && info.st_nlink != 1)) {
        close(fd); errno = EINVAL; return -1;
    }
    return fd;
}

static int stamp(int fd, time_t epoch) {
    struct timespec times[2] = {{epoch, 0}, {epoch, 0}};
    if (!futimens(fd, times)) return 0;
    if (errno != ENOSYS && errno != ENOTSUP && errno != EINVAL) return -1;
    struct timeval old_times[2] = {{epoch, 0}, {epoch, 0}};
    if (!futimes(fd, old_times)) return 0;
    if (errno != ENOSYS && errno != ENOTSUP && errno != EINVAL) return -1;
#ifdef SYS_utime
    /* Some vendor kernels omit utimensat but retain the original o32 call.
     * /proc/self/fd pins the already validated inode, not a mutable pathname. */
    char pinned[64];
    /* The original syscall uses kernel longs, not musl's 64-bit time_t. */
    struct { long actime, modtime; } old = {(long)epoch, (long)epoch};
    snprintf(pinned, sizeof pinned, "/proc/self/fd/%d", fd);
    return (int)syscall(SYS_utime, pinned, &old);
#else
    errno = ENOSYS;
    return -1;
#endif
}

int main(int argc, char **argv) {
    const char *value = getenv("LILSMOKE_BUILD_EPOCH");
    char *end = NULL;
    errno = 0;
    long long epoch = value ? strtoll(value, &end, 10) : 0;
    if (argc != 3 || strcmp(argv[1], "-c") || !value || !*value ||
        errno || !end || *end || epoch < 315619200LL || epoch > 2145916800LL ||
        (long long)(time_t)epoch != epoch) {
        fputs("SM64_BUILD_CLOCK: invalid shell invocation or logical epoch\n", stderr);
        return 77;
    }
    pid_t child = fork();
    if (child < 0) { perror("SM64_BUILD_CLOCK fork"); return 74; }
    if (!child) {
        execl("/bin/sh", "sh", "-c", argv[2], (char *)NULL);
        _exit(126);
    }
    int status;
    while (waitpid(child, &status, 0) < 0) {
        if (errno != EINTR) { perror("SM64_BUILD_CLOCK wait"); return 74; }
    }
    if (!WIFEXITED(status)) return WIFSIGNALED(status) ? 128 + WTERMSIG(status) : 74;
    if (WEXITSTATUS(status)) return WEXITSTATUS(status);
    const char *target = getenv("LILSMOKE_BUILD_TARGET");
    int fd = output_fd(target);
    if (fd == -2) return 0;
    /* A recipe may have several commands (syntax-check, then compile). The
     * primary output need not exist until a later successful command. */
    if (fd < 0 && errno == ENOENT) return 0;
    if (fd < 0) { perror("SM64_BUILD_CLOCK output"); return 74; }
    int result = stamp(fd, (time_t)epoch);
    int error = errno;
    close(fd);
    if (result) { errno = error; perror("SM64_BUILD_CLOCK timestamp"); return 74; }
    return 0;
}
