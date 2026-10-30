#ifndef LILSMOKE_KEYBOARD_H
#define LILSMOKE_KEYBOARD_H
#include <errno.h>
#include <fcntl.h>
#include <spawn.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#ifndef LILSMOKE_KEYBOARD_PROGRAM
#define LILSMOKE_KEYBOARD_PROGRAM "/usr/bin/lilsmoke-keyboard"
#endif

/* Foreground application API. Suspend the caller's display/input first.
 * Returns 0 on commit, 1 on cancel, -1 on error. No shell is involved. */
static inline int lilsmoke_request_text(const char *title, char *text,
                                       size_t capacity, int numeric, int secret) {
    int fd[2], status, error, overflow = 0;
    size_t used = 0;
    pid_t child;
    posix_spawn_file_actions_t actions;
    char limit[16], block[256];
    char *argv[9];
    int count = 0;
    extern char **environ;
    if (!text || capacity < 2 || capacity > 4097 || !title || strlen(title) > 160) {
        errno = EINVAL; return -1;
    }
    text[0] = '\0';
    snprintf(limit, sizeof(limit), "%zu", capacity - 1);
    argv[count++] = (char *)LILSMOKE_KEYBOARD_PROGRAM;
    argv[count++] = (char *)"--title"; argv[count++] = (char *)title;
    argv[count++] = (char *)"--max-length"; argv[count++] = limit;
    if (numeric) argv[count++] = (char *)"--numeric";
    if (secret) argv[count++] = (char *)"--secret";
    argv[count] = NULL;
    if (pipe(fd) != 0) return -1;
    /* Keep the pipe descriptors separate from standard input/output. */
    for (int i = 0; i < 2; ++i) {
        int replacement = fcntl(fd[i], F_DUPFD_CLOEXEC, 3);
        if (replacement < 0) {
            error = errno; close(fd[0]); close(fd[1]); errno = error; return -1;
        }
        close(fd[i]); fd[i] = replacement;
    }
    error = posix_spawn_file_actions_init(&actions);
    if (!error) {
        error = posix_spawn_file_actions_adddup2(&actions, fd[1], STDOUT_FILENO);
        if (!error) error = posix_spawn_file_actions_addclose(&actions, fd[0]);
        if (!error) error = posix_spawn_file_actions_addclose(&actions, fd[1]);
        if (!error) error = posix_spawn(&child, argv[0], &actions, NULL, argv, environ);
        posix_spawn_file_actions_destroy(&actions);
    }
    close(fd[1]);
    if (error) { close(fd[0]); errno = error; return -1; }
    error = 0;
    for (;;) {
        ssize_t n = read(fd[0], block, sizeof(block));
        if (n < 0 && errno == EINTR) continue;
        if (n < 0) { error = errno; break; }
        if (!n) break;
        for (ssize_t i = 0; i < n; ++i) {
            if (used < capacity - 1) text[used++] = block[i];
            else if (block[i] != '\n' || overflow) overflow = 1;
        }
    }
    close(fd[0]);
    while (waitpid(child, &status, 0) < 0) {
        if (errno != EINTR) { text[0] = '\0'; return -1; }
    }
    if (used && text[used - 1] == '\n') --used;
    text[used] = '\0';
    if (error || overflow || !WIFEXITED(status) || WEXITSTATUS(status) > 1) {
        text[0] = '\0'; errno = error ? error : EIO; return -1;
    }
    if (WEXITSTATUS(status) == 1) { text[0] = '\0'; return 1; }
    return 0;
}
#endif
