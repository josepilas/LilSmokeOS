#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <dirent.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
#include "../../board/segam-m8-v6-stock/m8-audio.h"

static int unreapable, term_calls, kill_calls;
static int socket_error, pipe2_error, pipe_error, fork_error, directory_error, buffer_error;
static int classic_pipe_calls;
static int test_socketpair(int domain, int type, int protocol, int fds[2]) {
    if (socket_error) { errno = socket_error; return -1; }
    return socketpair(domain, type, protocol, fds);
}
static int test_pipe2(int fds[2], int flags) {
    if (pipe2_error) { errno = pipe2_error; return -1; }
    return pipe2(fds, flags);
}
static int test_pipe(int fds[2]) {
    ++classic_pipe_calls;
    if (pipe_error) { errno = pipe_error; return -1; }
    return pipe(fds);
}
static pid_t test_fork(void) {
    if (fork_error) { errno = fork_error; return -1; }
    return fork();
}
static DIR *test_opendir(const char *path) {
    if (directory_error) { errno = directory_error; return NULL; }
    return opendir(path);
}
static int test_setsockopt(int fd, int level, int option, const void *value, socklen_t size) {
    if (buffer_error) { errno = buffer_error; return -1; }
    return setsockopt(fd, level, option, value, size);
}
static volatile sig_atomic_t entered;
static pid_t test_waitpid(pid_t pid, int *status, int options) {
    assert(pid > 0 && options == WNOHANG);
    return unreapable ? 0 : waitpid(pid, status, options);
}
static int test_kill(pid_t pid, int sig) {
    assert(pid > 0);
    if (sig == SIGTERM) ++term_calls;
    if (sig == SIGKILL) ++kill_calls;
    return kill(pid, sig);
}
#define waitpid test_waitpid
#define kill test_kill
#define socketpair test_socketpair
#define pipe2 test_pipe2
#define pipe test_pipe
#define fork test_fork
#define opendir test_opendir
#define setsockopt test_setsockopt
#include "../../package/opendingux-emulators/m8_pocketsnes_audio.c"
#undef waitpid
#undef kill
#undef socketpair
#undef pipe2
#undef pipe
#undef fork
#undef opendir
#undef setsockopt

static void signal_entered(int sig) { (void)sig; entered = 1; }
static void delay(unsigned ms) {
    struct timespec ts = {(time_t)(ms / 1000), (long)(ms % 1000) * 1000000};
    (void)nanosleep(&ts, NULL);
}
static void hang_in_fake_backend(void) {
    (void)signal(SIGTERM, SIG_IGN);
    (void)kill((pid_t)strtol(getenv("M8_PCM_TEST_PARENT"), NULL, 10), SIGUSR1);
    for (;;) pause();
}

/* Mock backend only: no production backend object, open, write or ioctl is
 * linked into this executable. Every backend entry point runs in the child. */
void m8_audio_init(struct m8_audio *a) { memset(a, 0, sizeof(*a)); a->fd = -1; }
int m8_audio_open(struct m8_audio *a, int volume) {
    (void)a;
    const char *setting = getenv("LILSMOKE_M8_VOLUME");
    assert(volume == (setting ? atoi(setting) : 25));
    assert(getpid() != (pid_t)strtol(getenv("M8_PCM_TEST_PARENT"), NULL, 10));
    assert(fcntl(100, F_GETFD) == -1 && errno == EBADF);
    const char *mode = getenv("M8_PCM_TEST_MODE");
    if (!strcmp(mode, "missing")) { errno = ENOENT; return -1; }
    if (!strcmp(mode, "hang-open")) hang_in_fake_backend();
    return 0;
}
int m8_audio_submit(struct m8_audio *a, const void *pcm, size_t frames) {
    assert(frames > 0 && frames <= 735);
    const int16_t *p = pcm;
    for (size_t i = 0; i < frames * 2; ++i) assert(p[i] == (i & 1 ? -1234 : 1234));
    a->bytes = frames * 4;
    return 1;
}
int m8_audio_pump(struct m8_audio *a) {
    const char *mode = getenv("M8_PCM_TEST_MODE");
    if (!strcmp(mode, "hang-pump")) hang_in_fake_backend();
    if (!strcmp(mode, "delayed")) delay(40);
    a->delivered_frames += a->bytes / 4;
    return 1;
}
int m8_audio_flush(struct m8_audio *a) { (void)a; return 0; }
void m8_audio_close(struct m8_audio *a) { (void)a; }

static void set_mode(const char *mode) {
    assert(m8_pcm_close() == 0);
    setenv("M8_PCM_TEST_MODE", mode, 1);
    entered = 0; term_calls = kill_calls = 0;
}
static void ready(void) {
    for (unsigned i = 0; i < 150 && !client.stats.ready; ++i) { service(); delay(2); }
    assert(client.stats.ready && m8_pcm_active());
    assert(fcntl(client.ipc.rx, F_GETFL) & O_NONBLOCK);
    assert(fcntl(client.ipc.tx, F_GETFL) & O_NONBLOCK);
    assert(fcntl(client.ipc.rx, F_GETFD) & FD_CLOEXEC);
    assert(fcntl(client.ipc.tx, F_GETFD) & FD_CLOEXEC);
}
static void entered_backend(void) {
    for (unsigned i = 0; i < 150 && !entered; ++i) delay(2);
    assert(entered);
}

static void pipe_framing_tests(void) {
    int fds[2];
    assert(new_pipe(fds) == 0);
    struct pcm_ipc ipc = {fds[0], fds[1], 1, 0, {0}};
    struct pcm_status sent = {READY, 0, 0, 0}, received;
    assert(write(ipc.tx, &sent, 4) == 4);
    assert(ipc_receive(&ipc, &received, sizeof(received)) == -1 && errno == EAGAIN && ipc.used == 4);
    assert(write(ipc.tx, (unsigned char *)&sent + 4, sizeof(sent) - 4) == (ssize_t)sizeof(sent) - 4);
    assert(ipc_receive(&ipc, &received, sizeof(received)) == (ssize_t)sizeof(received));
    assert(!memcmp(&received, &sent, sizeof(sent)) && !ipc.used);
    assert(write(ipc.tx, &sent, 4) == 4); close(ipc.tx); ipc.tx = -1;
    assert(ipc_receive(&ipc, &received, sizeof(received)) == -1 && errno == EAGAIN);
    assert(ipc_receive(&ipc, &received, sizeof(received)) == -1 && errno == EPROTO);
    ipc_close(&ipc);

    assert(new_pipe(fds) == 0); ipc.rx = fds[0]; ipc.tx = fds[1];
    close(ipc.rx); ipc.rx = -1;
    sigset_t before, after, pending;
    assert(sigprocmask(SIG_BLOCK, NULL, &before) == 0);
    assert(ipc_send(&ipc, &sent, sizeof(sent)) == -1 && errno == EPIPE);
    assert(sigprocmask(SIG_BLOCK, NULL, &after) == 0);
    assert(sigismember(&before, SIGPIPE) == sigismember(&after, SIGPIPE));
    assert(sigpending(&pending) == 0 && !sigismember(&pending, SIGPIPE));
    ipc_close(&ipc);
}

int main(int argc, char **argv) {
    int forced = argc > 1;
    if (forced) socket_error = EAFNOSUPPORT;
    if (argc > 1 && !strcmp(argv[1], "classic")) pipe2_error = ENOSYS;
    char parent[32]; snprintf(parent, sizeof(parent), "%ld", (long)getpid());
    setenv("M8_PCM_TEST_PARENT", parent, 1);
    (void)signal(SIGUSR1, signal_entered);
    int fixture = open("/dev/null", O_RDONLY | O_CLOEXEC);
    assert(fixture >= 0 && dup2(fixture, 100) == 100);
    close(fixture);
    int16_t pcm[1470];
    for (size_t i = 0; i < 1470; ++i) pcm[i] = i & 1 ? -1234 : 1234;
    unsetenv("LILSMOKE_M8_AUDIO");
    assert(!m8_pcm_requested() && !m8_pcm_init(44100, 2, 16));
    setenv("LILSMOKE_M8_AUDIO", "yes", 1);
    assert(!m8_pcm_init(44100, 2, 16));
    setenv("LILSMOKE_M8_AUDIO", "1", 1);
    setenv("LILSMOKE_M8_VOLUME", "51", 1);
    assert(!m8_pcm_init(44100, 2, 16));
    setenv("LILSMOKE_M8_VOLUME", "invalid", 1);
    assert(!m8_pcm_init(44100, 2, 16));
    unsetenv("LILSMOKE_M8_VOLUME");
    assert(!m8_pcm_init(48000, 2, 16));
    assert(!m8_pcm_init(44100, 1, 16));
    assert(!m8_pcm_init(44100, 2, 8));

    setenv("M8_PCM_TEST_MODE", "normal", 1);
    int saved_socket = socket_error, saved_pipe2 = pipe2_error;
    socket_error = EAFNOSUPPORT; pipe2_error = EMFILE;
    assert(!m8_pcm_init(44100, 2, 16) && client.stats.error == EMFILE);
    pipe2_error = ENOSYS; pipe_error = EMFILE;
    assert(!m8_pcm_init(44100, 2, 16) && client.stats.error == EMFILE);
    pipe_error = 0; pipe2_error = saved_pipe2; socket_error = saved_socket;
    directory_error = EACCES;
    assert(!m8_pcm_init(44100, 2, 16) && client.stats.error == EACCES);
    directory_error = 0; fork_error = EAGAIN;
    assert(!m8_pcm_init(44100, 2, 16) && client.stats.error == EAGAIN);
    fork_error = 0;
    pipe_framing_tests();

    set_mode("delayed"); assert(m8_pcm_init(44100, 2, 16)); ready();
    assert(client.stats.transport == (forced ? M8_PCM_IPC_PIPE : M8_PCM_IPC_SEQPACKET));
    assert(m8_pcm_send(NULL, 1) == -1);
    assert(m8_pcm_send(pcm, SIZE_MAX) == -1);
    assert(m8_pcm_send(NULL, 0) == 1);
    uint64_t start = milliseconds();
    unsigned accepted = 0;
    while (accepted < M8_PCM_INFLIGHT_LIMIT && m8_pcm_send(pcm, 735) == 1) ++accepted;
    assert(accepted >= 2 && accepted <= M8_PCM_INFLIGHT_LIMIT);
    for (unsigned i = 0; i < 100; ++i) assert(m8_pcm_send(pcm, 735) == 0);
    assert(milliseconds() - start < 30 && client.stats.inflight == accepted);
    for (unsigned i = 0; i < 1500 && client.stats.inflight; ++i) { service(); delay(2); }
    assert(client.stats.delivered_frames == accepted * 735 && !client.stats.inflight);
    assert(client.stats.dropped_frames >= 73500);
    assert(m8_pcm_close() == 0 && client.stats.worker == -1);

    if (!forced) {
        set_mode("normal"); buffer_error = ENOPROTOOPT;
        assert(m8_pcm_init(44100, 2, 16)); ready();
        assert(client.stats.transport == M8_PCM_IPC_PIPE);
        assert(m8_pcm_close() == 0); buffer_error = 0;
    }

    set_mode("missing"); assert(m8_pcm_init(44100, 2, 16));
    for (unsigned i = 0; i < 150 && m8_pcm_active(); ++i) delay(2);
    assert(!m8_pcm_active() && client.stats.error == ENOENT);
    assert(m8_pcm_send(pcm, 735) == 0 && m8_pcm_close() == 0);

    set_mode("hang-open"); assert(m8_pcm_init(44100, 2, 16)); entered_backend();
    start = milliseconds();
    assert(m8_pcm_send(pcm, 735) == 0 && milliseconds() - start < 30);
    assert(m8_pcm_close() == 0 && kill_calls == 1 && term_calls == 1);
    assert(milliseconds() - start < 250);

    set_mode("hang-pump"); assert(m8_pcm_init(44100, 2, 16)); ready();
    assert(m8_pcm_send(pcm, 735) == 1); entered_backend();
    start = milliseconds();
    accepted = 1;
    while (accepted < M8_PCM_INFLIGHT_LIMIT && m8_pcm_send(pcm, 735) == 1) ++accepted;
    assert(m8_pcm_send(pcm, 735) == 0);
    assert(milliseconds() - start < 30);
    assert(m8_pcm_close() == 0 && kill_calls == 1 && term_calls == 1);

    set_mode("hang-pump"); assert(m8_pcm_init(44100, 2, 16)); ready();
    assert(m8_pcm_send(pcm, 735) == 1); entered_backend();
    for (unsigned i = 0; i < 100 && client.stats.worker > 0; ++i) { service(); delay(5); }
    assert(client.stats.error == ETIMEDOUT && client.stats.worker == -1 && kill_calls == 1);

    set_mode("hang-open"); assert(m8_pcm_init(44100, 2, 16)); entered_backend();
    unreapable = 1; start = milliseconds();
    assert(m8_pcm_close() == -1 && client.stats.worker > 0 && kill_calls == 1);
    assert(milliseconds() - start < 250 && !m8_pcm_init(44100, 2, 16));
    unreapable = 0; assert(m8_pcm_close() == 0);

    setenv("LILSMOKE_M8_VOLUME", "20", 1);
    set_mode("normal"); assert(m8_pcm_init(44100, 2, 16)); ready();
    kill(client.stats.worker, SIGKILL); delay(5);
    assert(m8_pcm_send(pcm, 735) == 0); /* No SIGPIPE or peer-death wait. */
    assert(m8_pcm_close() == 0);
    close(100);
    if (argc > 1 && !strcmp(argv[1], "classic")) assert(classic_pipe_calls > 0);
    printf("PASS: transport=%s; init errno diagnostics, pipe partial framing/SIGPIPE, exact opt-in/volume/format, isolated backend, closed inherited fd, %u-packet bound, overload, missing/stuck device, TERM/KILL, bounded reap, retained PID, watchdog, peer EOF. No device ioctl linked or executed.\n", forced ? "pipe" : "seqpacket", (unsigned)M8_PCM_INFLIGHT_LIMIT);
    return 0;
}
