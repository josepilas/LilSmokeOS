#define _GNU_SOURCE
#include "owner-lock.h"
#include <assert.h>
#include <sys/wait.h>

static void touch_at(int fd, const char *name) {
    int file = openat(fd, name, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
    assert(file >= 0); assert(!close(file));
}
static unsigned count_entries(int fd) {
    int clone = openat(fd, ".", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    assert(clone >= 0); DIR *dir = fdopendir(clone); assert(dir);
    unsigned count = 0; struct dirent *e;
    while ((e = readdir(dir))) if (strcmp(e->d_name, ".") && strcmp(e->d_name, "..")) ++count;
    closedir(dir); return count;
}
static void unit(const char *path) {
    struct ls_owner_lock a = LS_OWNER_LOCK_INIT, b = LS_OWNER_LOCK_INIT;
    assert(!ls_owner_acquire(&a, path));
    uint64_t start; char state;
    assert(!ls_owner_process(getpid(), &start, &state) && start == a.start);
    assert(ls_owner_acquire(&b, path) == -1 && errno == EWOULDBLOCK);
    pid_t child = fork(); assert(child >= 0);
    if (!child) { ls_owner_release(&a); _exit(0); }
    assert(waitpid(child, NULL, 0) == child);
    assert(ls_owner_acquire(&b, path) == -1 && errno == EWOULDBLOCK);
    int root = openat(a.directory, ".", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    assert(root >= 0);
    ls_owner_release(&a); assert(!count_entries(root));
    /* PID reuse and interrupted initialization, without leases/timeouts. */
    char record[80]; snprintf(record, sizeof(record), "owner.%ld.%llu", (long)getpid(), (unsigned long long)(start + 1));
    assert(!mkdirat(root, record, 0700));
    int dead = openat(root, record, O_RDONLY | O_DIRECTORY | O_NOFOLLOW); assert(dead >= 0);
    touch_at(dead, "ticket.4"); close(dead);
    assert(!mkdirat(root, "owner.2147483647.1", 0700));
    assert(!ls_owner_acquire(&a, path)); ls_owner_release(&a); assert(!count_entries(root));
    /* A dead claim containing a symlink is refused, never followed/deleted. */
    assert(!mkdirat(root, record, 0700));
    dead = openat(root, record, O_RDONLY | O_DIRECTORY | O_NOFOLLOW); assert(dead >= 0);
    assert(!symlinkat("/etc/passwd", dead, "ticket.1"));
    assert(ls_owner_acquire(&a, path) == -1 && errno == EPERM);
    struct stat st; assert(!fstatat(dead, "ticket.1", &st, AT_SYMLINK_NOFOLLOW) && S_ISLNK(st.st_mode));
    assert(!unlinkat(dead, "ticket.1", 0)); close(dead); assert(!unlinkat(root, record, AT_REMOVEDIR));
    assert(!symlinkat("/tmp", root, record));
    assert(ls_owner_acquire(&a, path) == -1);
    assert(!unlinkat(root, record, 0));
    /* A genuinely live choosing process cannot be reclaimed as an empty lock. */
    snprintf(record, sizeof(record), "owner.%ld.%llu", (long)getppid(), (unsigned long long)0);
    assert(!ls_owner_process(getppid(), &start, &state));
    snprintf(record, sizeof(record), "owner.%ld.%llu", (long)getppid(), (unsigned long long)start);
    assert(!mkdirat(root, record, 0700));
    assert(ls_owner_acquire(&a, path) == -1 && errno == EWOULDBLOCK);
    dead = openat(root, record, O_RDONLY | O_DIRECTORY | O_NOFOLLOW); assert(dead >= 0);
    touch_at(dead, "ticket.18446744073709551615");
    assert(ls_owner_acquire(&a, path) == -1 && errno == EOVERFLOW);
    assert(!unlinkat(dead, "ticket.18446744073709551615", 0)); close(dead);
    assert(!unlinkat(root, record, AT_REMOVEDIR));
    assert(!count_entries(root)); close(root);
    /* Lock path and parent symlinks, traversal and insecure permissions fail. */
    char link[4096]; snprintf(link, sizeof(link), "%s.lock.d", path);
    assert(!rmdir(link)); assert(!symlink("/tmp", link));
    assert(ls_owner_acquire(&a, path) == -1); assert(!unlink(link));
    assert(!mkdir(link, 0777)); assert(!chmod(link, 0777));
    assert(ls_owner_acquire(&a, path) == -1 && errno == EPERM); assert(!rmdir(link));
    char parent[4096], redirected[4096];
    snprintf(parent, sizeof(parent), "%s-parent", path); assert(!symlink("/tmp", parent));
    assert(snprintf(redirected, sizeof(redirected), "%s/state", parent) < (int)sizeof(redirected));
    assert(ls_owner_acquire(&a, redirected) == -1); assert(!unlink(parent));
    assert(snprintf(redirected, sizeof(redirected), "%s/../escape", path) < (int)sizeof(redirected));
    assert(ls_owner_acquire(&a, redirected) == -1);
    puts("PASS owner unit: live exclusion, fork cleanup, PID reuse, empty crashed claim, symlink bounds, choosing peer, ticket overflow, cleanup");
}
int main(int argc, char **argv) {
    assert(argc == 3);
    if (!strcmp(argv[1], "unit")) { unit(argv[2]); return 0; }
    const char *gate = getenv("LOCK_QA_GATE");
    if (gate) { char byte; assert(read(atoi(gate), &byte, 1) == 1); }
    struct ls_owner_lock owner = LS_OWNER_LOCK_INIT;
    if (ls_owner_acquire(&owner, argv[2])) { fprintf(stderr, "BUSY errno=%d\n", errno); return 2; }
    printf("ACQUIRED pid=%ld ticket=%llu\n", (long)getpid(), (unsigned long long)owner.ticket); fflush(stdout);
    char byte; (void)!read(STDIN_FILENO, &byte, 1);
    ls_owner_release(&owner); puts("RELEASED"); return 0;
}
