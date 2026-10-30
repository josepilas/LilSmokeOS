/* SPDX-License-Identifier: MPL-2.0 */
/* Test-only preload forcing the vendor-kernel ENOSYS fallback. */
#define _GNU_SOURCE
#include <errno.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
int futimens(int fd, const struct timespec times[2]) {
    const char marker[] = "SM64_TEST_FUTIMENS_ENOSYS\n";
    if (write(2, marker, sizeof marker - 1) != sizeof marker - 1) _exit(99);
    (void)fd; (void)times; errno = ENOSYS; return -1;
}
int futimes(int fd, const struct timeval times[2]) {
    const char marker[] = "SM64_TEST_FUTIMES_ENOSYS\n";
    if (write(2, marker, sizeof marker - 1) != sizeof marker - 1) _exit(99);
    (void)fd; (void)times; errno = ENOSYS; return -1;
}
