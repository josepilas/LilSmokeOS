#ifndef LILSMOKE_NX_IMPORT_H
#define LILSMOKE_NX_IMPORT_H
#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <cstdlib>
#include <cstdio>
#include <cstring>
static int nx_import(const char *selected, char *out, size_t size) {
    const char *helper = getenv("LILSMOKE_NX_IMPORTER");
    if (!helper) helper = "/usr/lib/lilsmoke/lilsmoke-nx-data";
    int fds[2];
    if (access(helper, X_OK) || pipe(fds)) return -1;
    pid_t child = fork();
    if (!child) {
        close(fds[0]);
        if (dup2(fds[1], STDOUT_FILENO) < 0) _exit(126);
        close(fds[1]); execl(helper, helper, selected, (char *)NULL); _exit(127);
    }
    close(fds[1]);
    if (child < 0) { close(fds[0]); return -1; }
    size_t used = 0;
    int valid = 1, status = 0;
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    const time_t deadline = now.tv_sec + 300;
    for (;;) {
        clock_gettime(CLOCK_MONOTONIC, &now);
        if (now.tv_sec >= deadline || used + 1 >= size) { valid = 0; break; }
        struct pollfd fd = {fds[0], POLLIN, 0};
        int ready = poll(&fd, 1, (int)((deadline - now.tv_sec) * 1000));
        if (ready < 0 && errno == EINTR) continue;
        if (ready <= 0) { valid = 0; break; }
        ssize_t count = read(fds[0], out + used, size - used - 1);
        if (!count) break;
        if (count < 0) { if (errno == EINTR) continue; valid = 0; break; }
        used += (size_t)count;
    }
    close(fds[0]);
    for (;;) {
        pid_t result = waitpid(child, &status, WNOHANG);
        if (result == child) { child = -1; break; }
        clock_gettime(CLOCK_MONOTONIC, &now);
        if (!valid || now.tv_sec >= deadline || (result < 0 && errno != EINTR)) { valid = 0; break; }
        usleep(10000);
    }
    if (child > 0) { kill(child, SIGKILL); while (waitpid(child, &status, 0) < 0 && errno == EINTR) {} }
    if (!valid || !WIFEXITED(status) || WEXITSTATUS(status) || !used) return -1;
    if (out[used - 1] == '\n') --used;
    out[used] = 0;
    return used && out[0] == '/' && !strchr(out, '\n') && !strchr(out, '\r') && !memchr(out, 0, used) ? 0 : -1;
}
#endif
