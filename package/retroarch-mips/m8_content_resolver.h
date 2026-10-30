#ifndef M8_CONTENT_RESOLVER_H
#define M8_CONTENT_RESOLVER_H
#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static int m8_has_extension(const char *list, const char *wanted) {
    size_t length = strlen(wanted);
    const char *p = list;
    if (!p) return 0;
    while (*p) {
        const char *end = strchr(p, '|');
        if (!end) end = p + strlen(p);
        if ((size_t)(end - p) == length && strncmp(p, wanted, length) == 0) return 1;
        p = *end ? end + 1 : end;
    }
    return 0;
}
static int m8_resolve_content(const char *source, const char *extensions, int block_extract,
                              char *output, size_t capacity) {
    const char *enabled = getenv("LILSMOKE_USE_ROM_RESOLVER");
    const char *suffix = strrchr(source, '.');
    const char *system = NULL;
    if (!enabled || strcmp(enabled, "1") || !suffix || strchr(source, '#')) return 0;
    char ext[16]; size_t i;
    for (i = 0; suffix[i] && i + 1 < sizeof(ext); ++i) {
        char c = suffix[i]; ext[i] = c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c;
    }
    ext[i] = 0;
    if (strcmp(ext,".zip") && strcmp(ext,".7z") && strcmp(ext,".rar") && strcmp(ext,".tar") &&
        strcmp(ext,".gz") && strcmp(ext,".bz2") && strcmp(ext,".xz") && strcmp(ext,".tgz") &&
        strcmp(ext,".tbz2") && strcmp(ext,".txz")) return 0;
    if (block_extract) system = "arcade";
    else if (m8_has_extension(extensions,"gba") || m8_has_extension(extensions,"agb")) system="gba";
    else if (m8_has_extension(extensions,"sfc") || m8_has_extension(extensions,"smc")) system="snes";
    else if (m8_has_extension(extensions,"nes")) system="nes";
    else if (m8_has_extension(extensions,"gbc") || m8_has_extension(extensions,"gb")) system="gbc";
    else if (m8_has_extension(extensions,"md") || m8_has_extension(extensions,"gen")) system="md";
    else if (m8_has_extension(extensions,"sms")) system="sms";
    else if (m8_has_extension(extensions,"gg")) system="gg";
    else if (m8_has_extension(extensions,"pce")) system="pce";
    else if (m8_has_extension(extensions,"lnx")) system="lynx";
    else if (m8_has_extension(extensions,"z64") || m8_has_extension(extensions,"n64")) system="n64";
    else if (m8_has_extension(extensions,"cue") || m8_has_extension(extensions,"pbp")) system="psx";
    if (!system) return -1;
    int fds[2]; if (pipe(fds)) return -1;
    pid_t child = fork();
    if (child < 0) { close(fds[0]); close(fds[1]); return -1; }
    if (!child) {
        char *args[] = {"lilsmoke-rom", (char *)system, (char *)source, NULL};
        close(fds[0]);
        if (dup2(fds[1], STDOUT_FILENO) < 0) _exit(127);
        close(fds[1]);
        execv("/usr/lib/lilsmoke/lilsmoke-rom", args);
        _exit(127);
    }
    close(fds[1]);
    size_t used = 0; int status = 0, failed = 0;
    for (;;) {
        struct pollfd p = {fds[0], POLLIN, 0};
        int ready = poll(&p, 1, 120000);
        if (ready < 0 && errno == EINTR) continue;
        if (ready <= 0 || used + 1 >= capacity) { failed = 1; break; }
        ssize_t n = read(fds[0], output + used, capacity - used - 1);
        if (n < 0 && errno == EINTR) continue;
        if (n < 0) { failed = 1; break; }
        if (!n) break;
        used += n;
    }
    close(fds[0]);
    if (failed) kill(child, SIGKILL);
    while (waitpid(child, &status, 0) < 0) if (errno != EINTR) { failed = 1; break; }
    if (failed || !WIFEXITED(status) || WEXITSTATUS(status) || !used) return -1;
    output[used] = 0;
    while (used && (output[used-1] == '\n' || output[used-1] == '\r')) output[--used] = 0;
    return used && output[0] == '/' ? 1 : -1;
}
#endif
