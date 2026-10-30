#ifndef LILSMOKE_OWNER_LOCK_H
#define LILSMOKE_OWNER_LOCK_H
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

struct ls_owner_lock {
    int parent, directory, claim;
    pid_t pid;
    uint64_t start, ticket;
    char state[NAME_MAX + 1], name[NAME_MAX + 1], record[80], ticket_file[40];
};
#define LS_OWNER_LOCK_INIT { .parent = -1, .directory = -1, .claim = -1 }

/* /proc stat field 22 distinguishes a live owner from a reused PID. */
static int ls_owner_process(pid_t pid, uint64_t *start, char *state) {
    char path[64], data[4096], *p, *end;
    snprintf(path, sizeof(path), "/proc/%ld", (long)pid);
    int process = open(path, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (process < 0) return -1;
    int fd = openat(process, "stat", O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    int open_error = errno; close(process); errno = open_error;
    if (fd < 0) return -1;
    ssize_t size;
    do { size = read(fd, data, sizeof(data) - 1); } while (size < 0 && errno == EINTR);
    int error = errno; close(fd); errno = error;
    if (size <= 0) { if (!size) errno = EIO; return -1; }
    data[size] = 0; p = strrchr(data, ')');
    if (!p || p[1] != ' ' || !p[2] || p[3] != ' ') { errno = EIO; return -1; }
    *state = p[2]; p += 4;
    for (unsigned field = 4; field < 22; ++field) {
        p = strchr(p, ' ');
        if (!p) { errno = EIO; return -1; }
        while (*p == ' ') ++p;
    }
    errno = 0; unsigned long long value = strtoull(p, &end, 10);
    if (errno || end == p || (*end != ' ' && *end != '\n')) { errno = EIO; return -1; }
    *start = (uint64_t)value; return 0;
}

static int ls_owner_same(int parent, const char *name, int fd) {
    struct stat held, named;
    return !fstat(fd, &held) && !fstatat(parent, name, &named, AT_SYMLINK_NOFOLLOW) &&
        S_ISDIR(named.st_mode) && held.st_dev == named.st_dev && held.st_ino == named.st_ino;
}

static void ls_owner_release(struct ls_owner_lock *lock) {
    int error = errno;
    if (lock->pid == getpid() && lock->record[0]) {
        if (lock->claim >= 0 && lock->ticket_file[0]) (void)unlinkat(lock->claim, lock->ticket_file, 0);
        if (lock->directory >= 0) (void)unlinkat(lock->directory, lock->record, AT_REMOVEDIR);
    }
    if (lock->claim >= 0) close(lock->claim);
    if (lock->directory >= 0) close(lock->directory);
    if (lock->parent >= 0) close(lock->parent);
    lock->parent = lock->directory = lock->claim = -1;
    lock->record[0] = lock->ticket_file[0] = 0; errno = error;
}

/* Anchor operations to a private parent, without traversing symlinks or '..'. */
static int ls_owner_parent(struct ls_owner_lock *lock, const char *path) {
    char copy[4096];
    if (!path || !*path || strlen(path) >= sizeof(copy)) { errno = ENAMETOOLONG; return -1; }
    strcpy(copy, path);
    int parent = open(path[0] == '/' ? "/" : ".", O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (parent < 0) return -1;
    char *part = copy + (path[0] == '/');
    for (;;) {
        char *slash = strchr(part, '/');
        if (slash) *slash = 0;
        if (!*part || !strcmp(part, ".") || !strcmp(part, "..")) { errno = EINVAL; break; }
        if (!slash) {
            struct stat st;
            if (strlen(part) + sizeof(".lock.d") > sizeof(lock->name)) { errno = ENAMETOOLONG; break; }
            if (fstat(parent, &st)) break;
            if (st.st_uid != geteuid() || (st.st_mode & 0022)) { errno = EPERM; break; }
            strcpy(lock->state, part);
            snprintf(lock->name, sizeof(lock->name), "%s.lock.d", part);
            lock->parent = parent; return 0;
        }
        int next = openat(parent, part, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
        if (next < 0) break;
        close(parent); parent = next; part = slash + 1;
    }
    int error = errno; close(parent); errno = error; return -1;
}

static int ls_owner_parse(const char *name, pid_t *pid, uint64_t *start) {
    if (strncmp(name, "owner.", 6) || name[6] < '0' || name[6] > '9') return 0;
    char *end;
    errno = 0; unsigned long value = strtoul(name + 6, &end, 10);
    if (errno || *end != '.' || !value || value > INT_MAX) return 0;
    const char *tail = end + 1;
    if (*tail < '0' || *tail > '9') return 0;
    errno = 0; unsigned long long stamp = strtoull(tail, &end, 10);
    if (errno || *end) return 0;
    *pid = (pid_t)value; *start = (uint64_t)stamp; return 1;
}

static int ls_owner_ticket(int fd, uint64_t *ticket, char *filename) {
    int scan = openat(fd, ".", O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (scan < 0) return -1;
    DIR *dir = fdopendir(scan);
    if (!dir) { int error = errno; close(scan); errno = error; return -1; }
    *ticket = 0; filename[0] = 0;
    unsigned count = 0;
    struct dirent *entry;
    errno = 0;
    while ((entry = readdir(dir)) != NULL) {
        struct stat st; char *end;
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
        if (++count > 1 || strncmp(entry->d_name, "ticket.", 7) ||
            entry->d_name[7] < '0' || entry->d_name[7] > '9' || strlen(entry->d_name) >= 40) { errno = EPERM; break; }
        unsigned long long value = strtoull(entry->d_name + 7, &end, 10);
        if (errno || *end || !value || fstatat(fd, entry->d_name, &st, AT_SYMLINK_NOFOLLOW)) {
            if (!errno) errno = EPERM;
            break;
        }
        if (!S_ISREG(st.st_mode) || st.st_uid != geteuid() || (st.st_mode & 0077) || st.st_size) { errno = EPERM; break; }
        *ticket = value; strcpy(filename, entry->d_name);
    }
    int error = errno; closedir(dir); errno = error; return error ? -1 : 0;
}

/* Filesystem Lamport bakery: register PID/starttime before choosing a ticket,
   then wait for choosing peers and yield to the smallest (ticket, PID, start).
   Keep the container persistent and reap only dead, uniquely named claims:
   no stale-directory removal/creation ABA race and no advisory-lock syscalls. */
static int ls_owner_scan(struct ls_owner_lock *lock, int choosing, uint64_t *maximum) {
    int scan = openat(lock->directory, ".", O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (scan < 0) return -1;
    DIR *dir = fdopendir(scan);
    if (!dir) { int error = errno; close(scan); errno = error; return -1; }
    unsigned count = 0; int waiting = 0, error = 0;
    struct dirent *entry;
    while (errno = 0, (entry = readdir(dir)) != NULL) {
        pid_t pid; uint64_t start, observed, ticket; char state, filename[40]; struct stat st;
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
        if (++count > 128 || !ls_owner_parse(entry->d_name, &pid, &start)) { error = EPERM; break; }
        if (!strcmp(entry->d_name, lock->record)) continue;
        int fd = openat(lock->directory, entry->d_name, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
        if (fd < 0) { if (errno == ENOENT) continue; error = errno; break; }
        if (fstat(fd, &st) || st.st_uid != geteuid() || (st.st_mode & 0077)) { error = EPERM; close(fd); break; }
        int alive = ls_owner_process(pid, &observed, &state) == 0;
        if (!alive && errno != ENOENT && errno != ESRCH) { error = errno; close(fd); break; }
        int dead = !alive || observed != start || state == 'Z' || state == 'X';
        if (ls_owner_ticket(fd, &ticket, filename)) {
            error = errno; close(fd);
            if (error == ENOENT) { error = 0; continue; }
            break;
        }
        if (dead) {
            if (filename[0]) (void)unlinkat(fd, filename, 0);
            (void)unlinkat(lock->directory, entry->d_name, AT_REMOVEDIR);
        } else if (choosing) {
            if (ticket > *maximum) *maximum = ticket;
        } else if (!ticket) {
            waiting = 1;
        } else if (ticket < lock->ticket || (ticket == lock->ticket &&
                   (pid < lock->pid || (pid == lock->pid && start < lock->start)))) {
            error = EWOULDBLOCK;
        }
        close(fd);
        if (error) break;
    }
    if (!error && errno) error = errno;
    closedir(dir);
    if (error) { errno = error; return -1; }
    return waiting;
}

static int ls_owner_acquire(struct ls_owner_lock *lock, const char *path) {
    char state, record[80]; uint64_t maximum = 0;
    lock->parent = lock->directory = lock->claim = -1;
    lock->record[0] = lock->ticket_file[0] = 0; lock->pid = getpid();
    if (ls_owner_process(lock->pid, &lock->start, &state) || ls_owner_parent(lock, path)) return -1;
    if (mkdirat(lock->parent, lock->name, 0700) && errno != EEXIST) goto fail;
    lock->directory = openat(lock->parent, lock->name, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (lock->directory < 0) goto fail;
    struct stat st;
    if (fstat(lock->directory, &st)) goto fail;
    if (st.st_uid != geteuid() || (st.st_mode & 0077)) { errno = EPERM; goto fail; }
    snprintf(record, sizeof(record), "owner.%ld.%llu", (long)lock->pid, (unsigned long long)lock->start);
    if (mkdirat(lock->directory, record, 0700)) { if (errno == EEXIST) errno = EWOULDBLOCK; goto fail; }
    strcpy(lock->record, record);
    lock->claim = openat(lock->directory, record, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (lock->claim < 0 || ls_owner_scan(lock, 1, &maximum) < 0) goto fail;
    if (maximum == UINT64_MAX) { errno = EOVERFLOW; goto fail; }
    lock->ticket = maximum + 1;
    snprintf(lock->ticket_file, sizeof(lock->ticket_file), "ticket.%llu", (unsigned long long)lock->ticket);
    int fd = openat(lock->claim, lock->ticket_file, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (fd < 0) goto fail;
    close(fd);
    for (unsigned attempt = 0; attempt < 64; ++attempt) {
        int result = ls_owner_scan(lock, 0, &maximum);
        if (result < 0) goto fail;
        if (!result) {
            if (ls_owner_same(lock->parent, lock->name, lock->directory) &&
                ls_owner_same(lock->directory, lock->record, lock->claim)) return 0;
            errno = EBUSY; goto fail;
        }
        struct timespec pause = {0, 4000000};
        while (nanosleep(&pause, &pause) && errno == EINTR) {}
    }
    errno = EWOULDBLOCK;
fail: {
        int error = errno; ls_owner_release(lock); errno = error; return -1;
    }
}
#endif
