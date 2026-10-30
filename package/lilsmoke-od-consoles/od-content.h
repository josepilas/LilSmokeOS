#ifndef LILSMOKE_OD_CONTENT_H
#define LILSMOKE_OD_CONTENT_H
#include <errno.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#ifndef OD_RESOLVER_TIMEOUT_MS
#define OD_RESOLVER_TIMEOUT_MS 300000
#endif

static unsigned long long od_millis(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (unsigned long long)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

static const char *od_asset_path(const char *name)
{
    static char path[PATH_MAX];
    const char *directory = getenv("LILSMOKE_OD_SHARE_DIR");
    if (!directory || !*directory) {
        const char *engine = getenv("LILSMOKE_OD_ENGINE");
        if (!engine || !*engine) return name;
        if (snprintf(path, sizeof(path), "/storage/emulators/share/%s/%s", engine, name) >= (int)sizeof(path)) return NULL;
        return path;
    }
    if (snprintf(path, sizeof(path), "%s/%s", directory, name) >= (int)sizeof(path)) return NULL;
    return path;
}

static const char *od_save_path(const char *rom, const char *suffix)
{
    static char result[PATH_MAX];
    char directory[PATH_MAX];
    const char *home = getenv("HOME"), *base = strrchr(rom, '/');
    const unsigned char *scan = (const unsigned char *)rom;
    unsigned long long hash = 14695981039346656037ULL;
    while (*scan) { hash ^= *scan++; hash *= 1099511628211ULL; }
    if (!home || !*home) return NULL;
    if (snprintf(directory, sizeof(directory), "%s/saves", home) >= (int)sizeof(directory)) return NULL;
    if (mkdir(directory, 0755) && errno != EEXIST) return NULL;
    if (snprintf(result, sizeof(result), "%s/%.160s-%016llx%s", directory,
                 base ? base + 1 : rom, hash, suffix) >= (int)sizeof(result)) return NULL;
    return result;
}

static const char *od_state_path(const char *rom, int slot)
{
    static char result[PATH_MAX];
    char directory[PATH_MAX];
    const char *home = getenv("HOME"), *base = rom ? strrchr(rom, '/') : NULL;
    const unsigned char *scan = (const unsigned char *)rom;
    unsigned long long hash = 14695981039346656037ULL;
    if (!home || !*home || !rom || !*rom || slot < 0 || slot > 99) return NULL;
    while (*scan) { hash ^= *scan++; hash *= 1099511628211ULL; }
    if (snprintf(directory, sizeof(directory), "%s/states", home) >= (int)sizeof(directory)) return NULL;
    if (mkdir(directory, 0755) && errno != EEXIST) return NULL;
    if (snprintf(result, sizeof(result), "%s/%.128s-%016llx.slot%d.state", directory,
                 base ? base + 1 : rom, hash, slot) >= (int)sizeof(result)) return NULL;
    return result;
}

static const char *od_system(const char *fallback)
{
    const char *value = getenv("LILSMOKE_OD_SYSTEM");
    return value && *value ? value : fallback;
}

static const char *od_rom_directory(const char *fallback)
{
    static char path[PATH_MAX];
    const char *value = getenv("LILSMOKE_OD_ROM_DIR");
    if (!value || !*value) value = getenv("ROM_DIR");
    if (value && *value) return value;
    snprintf(path, sizeof(path), "/storage/roms/%s", od_system(fallback));
    return path;
}

static int od_is_archive(const char *path)
{
    static const char *const formats[] = {
        ".zip", ".7z", ".rar", ".tar", ".gz", ".bz2", ".xz", ".tgz", ".tbz2", ".txz"
    };
    const char *extension = path ? strrchr(path, '.') : NULL;
    size_t i;
    if (!extension) return 0;
    for (i = 0; i < sizeof(formats) / sizeof(formats[0]); ++i)
        if (!strcasecmp(extension, formats[i])) return 1;
    return 0;
}

/* The OS resolver owns archive safety, extraction, and content selection. */
static const char *od_resolve_content(const char *path, const char *fallback)
{
    static char result[PATH_MAX];
    const char *helper;
    int descriptors[2], status = 0, valid = 1;
    size_t size = 0;
    unsigned long long deadline;
    struct stat file;
    pid_t child;
    if (!path) return NULL;
    if (!od_is_archive(path)) return path;
    helper = getenv("LILSMOKE_ROM_RESOLVER");
    if (!helper || !*helper) helper = "/usr/lib/lilsmoke/lilsmoke-rom";
    if (access(helper, X_OK) || pipe(descriptors)) return NULL;
    child = fork();
    if (child == 0) {
        close(descriptors[0]);
        if (dup2(descriptors[1], STDOUT_FILENO) < 0) _exit(126);
        close(descriptors[1]);
        execl(helper, helper, od_system(fallback), path, (char *)NULL);
        _exit(127);
    }
    close(descriptors[1]);
    if (child < 0) { close(descriptors[0]); return NULL; }
    deadline = od_millis() + OD_RESOLVER_TIMEOUT_MS;
    while (valid) {
        struct pollfd descriptor;
        ssize_t count;
        descriptor.fd = descriptors[0];
        descriptor.events = POLLIN;
        descriptor.revents = 0;
        unsigned long long now = od_millis();
        int ready;
        if (now >= deadline) { valid = 0; break; }
        ready = poll(&descriptor, 1, (int)(deadline - now));
        if (ready < 0 && errno == EINTR) continue;
        if (ready <= 0) { valid = 0; break; }
        count = read(descriptors[0], result + size, sizeof(result) - size - 1);
        if (count == 0) break;
        if (count < 0) { if (errno == EINTR) continue; valid = 0; break; }
        size += (size_t)count;
        if (size == sizeof(result) - 1) { valid = 0; break; }
    }
    close(descriptors[0]);
    while (valid) {
        pid_t reaped = waitpid(child, &status, WNOHANG);
        if (reaped == child) { child = -1; break; }
        if (reaped < 0 && errno != EINTR) { valid = 0; break; }
        if (od_millis() >= deadline) { valid = 0; break; }
        usleep(5000);
    }
    if (child > 0) {
        kill(child, SIGKILL);
        while (waitpid(child, &status, 0) < 0) if (errno != EINTR) return NULL;
    }
    if (size && result[size - 1] == '\n') --size;
    result[size] = '\0';
    if (!valid || !WIFEXITED(status) || WEXITSTATUS(status) || !size ||
        result[0] != '/' || memchr(result, '\0', size) || strchr(result, '\n') || strchr(result, '\r') ||
        stat(result, &file) || !S_ISREG(file.st_mode) || access(result, R_OK)) {
        fprintf(stderr, "OpenDingux archive resolution failed: %s\n", path);
        return NULL;
    }
    return result;
}
#endif
