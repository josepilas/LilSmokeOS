#define _GNU_SOURCE
#include "m8_pocketsnes_audio.h"
#include "m8-audio.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#ifndef M8_PCM_BACKEND_TIMEOUT_MS
#define M8_PCM_BACKEND_TIMEOUT_MS 100U
#endif
#ifndef M8_PCM_BACKEND_MAX_POLLS
#define M8_PCM_BACKEND_MAX_POLLS 64U
#endif
#ifndef M8_PCM_ACK_TIMEOUT_MS
#define M8_PCM_ACK_TIMEOUT_MS 300U
#endif
_Static_assert(M8_PCM_BACKEND_TIMEOUT_MS <= 1000 && M8_PCM_ACK_TIMEOUT_MS <= 2000,
               "bounded audio watchdogs");

enum { OFF, STARTING, RUNNING, TERMINATING, KILLING, PARKED };
enum { READY = 1, ACK, FAILED };
enum { SETUP = 1, BACKEND_OPEN, BACKEND_SUBMIT, BACKEND_PUMP, PROTOCOL };
struct pcm_packet { uint32_t frames; int16_t pcm[M8_PCM_CHUNK_FRAMES * 2]; };
struct pcm_status { uint32_t kind, frames; int32_t error; uint32_t stage; };
struct pcm_ipc {
    int rx, tx, pipe;
    size_t used;
    unsigned char buffer[sizeof(struct pcm_packet)];
};
_Static_assert(sizeof(struct pcm_packet) <= PIPE_BUF, "atomic PCM pipe record");
_Static_assert(M8_PCM_INFLIGHT_LIMIT >= 2 && M8_PCM_INFLIGHT_LIMIT <= 16, "bounded PCM credits");
static struct {
    struct m8_pcm_stats stats;
    struct pcm_ipc ipc;
    int phase;
    uint64_t deadline;
    uint32_t outstanding[M8_PCM_INFLIGHT_LIMIT];
    int reported;
} client = {{0, 0, 0, 0, 0, 0, -1, M8_PCM_IPC_NONE}, {-1, -1, 0, 0, {0}}, OFF, 0, {0, 0}, 1};

static const char *backend_stage(unsigned stage) {
    switch (stage) {
        case SETUP: return "worker-setup";
        case BACKEND_OPEN: return "backend-open";
        case BACKEND_SUBMIT: return "backend-submit";
        case BACKEND_PUMP: return "backend-pump";
        default: return "ipc-protocol";
    }
}

static void ipc_close(struct pcm_ipc *ipc) {
    if (ipc->tx >= 0 && ipc->tx != ipc->rx) (void)close(ipc->tx);
    if (ipc->rx >= 0) (void)close(ipc->rx);
    ipc->rx = ipc->tx = -1;
    ipc->used = 0;
}

static int fd_flags(int fd) {
    int flags = fcntl(fd, F_GETFL);
    int descriptor = fcntl(fd, F_GETFD);
    return flags >= 0 && descriptor >= 0 &&
           fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0 &&
           fcntl(fd, F_SETFD, descriptor | FD_CLOEXEC) == 0 ? 0 : -1;
}

static int new_pipe(int fds[2]) {
    if (pipe2(fds, O_NONBLOCK | O_CLOEXEC) == 0) return 0;
    if (errno != ENOSYS && errno != EINVAL) return -1;
    if (pipe(fds) < 0) return -1;
    if (fd_flags(fds[0]) == 0 && fd_flags(fds[1]) == 0) return 0;
    int error = errno;
    (void)close(fds[0]); (void)close(fds[1]); errno = error;
    return -1;
}

static int ipc_pair(struct pcm_ipc *parent, struct pcm_ipc *child, const char **stage) {
    int sockets[2], data[2], status[2];
    *stage = "socketpair";
    if (socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_NONBLOCK | SOCK_CLOEXEC, 0, sockets) == 0) {
        int size = M8_PCM_INFLIGHT_LIMIT * (int)sizeof(struct pcm_packet), valid = 1;
        *stage = "socket-buffer";
        for (unsigned i = 0; i < 2; ++i)
            if (setsockopt(sockets[i], SOL_SOCKET, SO_SNDBUF, &size, sizeof(size)) < 0 ||
                setsockopt(sockets[i], SOL_SOCKET, SO_RCVBUF, &size, sizeof(size)) < 0) { valid = 0; break; }
        if (valid) {
            parent->rx = parent->tx = sockets[0]; child->rx = child->tx = sockets[1];
            return 0;
        }
        int error = errno;
        (void)close(sockets[0]); (void)close(sockets[1]); errno = error;
    }
    fprintf(stderr, "POCKETSNES_AUDIO ipc-fallback stage=%s errno=%d error=%s next=pipe\n", *stage, errno, strerror(errno));
    *stage = "pipe-data";
    if (new_pipe(data) < 0) return -1;
    *stage = "pipe-status";
    if (new_pipe(status) < 0) {
        int error = errno; (void)close(data[0]); (void)close(data[1]); errno = error; return -1;
    }
    parent->rx = status[0]; parent->tx = data[1]; parent->pipe = 1;
    child->rx = data[0]; child->tx = status[1]; child->pipe = 1;
    *stage = "pipe-atomic-size";
    if (fpathconf(parent->tx, _PC_PIPE_BUF) < (long)sizeof(struct pcm_packet) ||
        fpathconf(child->tx, _PC_PIPE_BUF) < (long)sizeof(struct pcm_status)) {
        errno = ENOTSUP; return -1;
    }
    return 0;
}

static ssize_t ipc_send(struct pcm_ipc *ipc, const void *data, size_t bytes) {
    if (!ipc->pipe) return send(ipc->tx, data, bytes, MSG_DONTWAIT | MSG_NOSIGNAL);
    /* Pipe writes have no MSG_NOSIGNAL. Block/consume only this thread's new
     * synchronous SIGPIPE; preserve the caller's mask and existing signal. */
    sigset_t blocked, old, pending;
    sigemptyset(&blocked); sigaddset(&blocked, SIGPIPE);
    if (sigprocmask(SIG_BLOCK, &blocked, &old) < 0) return -1;
    if (sigpending(&pending) < 0) {
        int error = errno; (void)sigprocmask(SIG_SETMASK, &old, NULL); errno = error; return -1;
    }
    int already_pending = sigismember(&pending, SIGPIPE);
    ssize_t result = write(ipc->tx, data, bytes);
    int error = errno;
    if (result < 0 && error == EPIPE && !already_pending) {
        struct timespec zero = {0, 0};
        (void)sigtimedwait(&blocked, NULL, &zero);
    }
    (void)sigprocmask(SIG_SETMASK, &old, NULL);
    errno = error;
    return result;
}

static ssize_t ipc_receive(struct pcm_ipc *ipc, void *data, size_t bytes) {
    if (!ipc->pipe) return recv(ipc->rx, data, bytes, MSG_DONTWAIT | MSG_TRUNC);
    ssize_t result = read(ipc->rx, ipc->buffer + ipc->used, bytes - ipc->used);
    if (result > 0) {
        ipc->used += (size_t)result;
        if (ipc->used == bytes) {
            memcpy(data, ipc->buffer, bytes); ipc->used = 0; return (ssize_t)bytes;
        }
        errno = EAGAIN; return -1;
    }
    if (result == 0 && ipc->used) { errno = EPROTO; return -1; }
    return result;
}

static uint64_t milliseconds(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) < 0) return 0;
    return (uint64_t)ts.tv_sec * 1000U + (uint64_t)ts.tv_nsec / 1000000U;
}

static int notify_parent(struct pcm_ipc *ipc, unsigned kind, unsigned frames, int error, unsigned stage) {
    struct pcm_status status = {kind, frames, error, stage};
    return ipc_send(ipc, &status, sizeof(status)) == (ssize_t)sizeof(status);
}

static void worker(struct pcm_ipc *ipc, pid_t parent, const int *fds, unsigned count, int volume) {
    struct sigaction action;
    sigset_t empty;
    memset(&action, 0, sizeof(action));
    action.sa_handler = SIG_DFL;
    sigemptyset(&action.sa_mask);
    (void)sigaction(SIGTERM, &action, NULL);
    (void)sigaction(SIGINT, &action, NULL);
    sigemptyset(&empty);
    (void)sigprocmask(SIG_SETMASK, &empty, NULL);
    if (prctl(PR_SET_PDEATHSIG, SIGKILL) < 0 || getppid() != parent) {
        (void)notify_parent(ipc, FAILED, 0, errno ? errno : ECHILD, SETUP); _exit(1);
    }
    /* Do not pin game/DirectFB/device/SD-log descriptors while isolated. All
     * allocation and /proc enumeration happened before fork; no SDL in child. */
    for (unsigned i = 0; i < count; ++i)
        if (fds[i] != ipc->rx && fds[i] != ipc->tx) (void)close(fds[i]);
    struct m8_audio audio;
    m8_audio_init(&audio);
    if (m8_audio_open(&audio, volume) < 0) {
        (void)notify_parent(ipc, FAILED, 0, errno, BACKEND_OPEN);
        _exit(1);
    }
    if (!notify_parent(ipc, READY, 0, 0, 0)) _exit(1);
    for (;;) {
        struct pollfd pollfd = {ipc->rx, POLLIN, 0};
        int result = poll(&pollfd, 1, 100);
        if (result < 0 && errno == EINTR) continue;
        if (result < 0 || (pollfd.revents & (POLLHUP | POLLERR | POLLNVAL))) break;
        if (!result) continue;
        struct pcm_packet packet;
        ssize_t bytes = ipc_receive(ipc, &packet, sizeof(packet));
        if (bytes < 0 && (errno == EAGAIN || errno == EINTR)) continue;
        if (bytes < 4 || bytes > (ssize_t)sizeof(packet) || !packet.frames ||
            packet.frames > M8_PCM_CHUNK_FRAMES ||
            bytes != (ipc->pipe ? (ssize_t)sizeof(packet) : 4 + (ssize_t)packet.frames * 4)) {
            (void)notify_parent(ipc, FAILED, 0, EPROTO, PROTOCOL); break;
        }
        uint64_t deadline = milliseconds() + M8_PCM_BACKEND_TIMEOUT_MS;
        int accepted = 0, delivered = 0;
        for (unsigned attempts = 0; attempts < M8_PCM_BACKEND_MAX_POLLS && milliseconds() < deadline; ++attempts) {
            result = accepted ? m8_audio_pump(&audio) : m8_audio_submit(&audio, packet.pcm, packet.frames);
            if (result < 0) {
                (void)notify_parent(ipc, FAILED, 0, errno, accepted ? BACKEND_PUMP : BACKEND_SUBMIT);
                _exit(1);
            }
            if (result == 1) {
                if (accepted) { delivered = 1; break; }
                accepted = 1;
                continue;
            }
            struct timespec delay = {0, 2000000};
            (void)nanosleep(&delay, NULL);
        }
        if (!delivered) {
            (void)notify_parent(ipc, FAILED, 0, ETIMEDOUT, accepted ? BACKEND_PUMP : BACKEND_SUBMIT);
            _exit(1);
        }
        if (!notify_parent(ipc, ACK, packet.frames, 0, 0)) break;
    }
    m8_audio_close(&audio); /* Even close is isolated from the game. */
    _exit(0);
}

static void begin_stop(int error) {
    if (error && !client.stats.error) {
        client.stats.error = error;
        fprintf(stderr, "POCKETSNES_AUDIO native-disabled errno=%d\n", error);
    }
    ipc_close(&client.ipc);
    client.stats.ready = 0;
    for (unsigned i = 0; i < client.stats.inflight; ++i)
        client.stats.dropped_frames += client.outstanding[i];
    memset(client.outstanding, 0, sizeof(client.outstanding));
    client.stats.inflight = 0;
    if (client.stats.worker > 0 && client.phase < TERMINATING) {
        (void)kill(client.stats.worker, SIGTERM);
        client.phase = TERMINATING;
        client.deadline = milliseconds() + 50;
    }
}

static void service(void) {
    uint64_t now = milliseconds();
    if (client.ipc.rx >= 0) {
        for (unsigned i = 0; i < 4; ++i) {
            struct pcm_status status;
            ssize_t n = ipc_receive(&client.ipc, &status, sizeof(status));
            if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) break;
            if (n != (ssize_t)sizeof(status)) { begin_stop(n < 0 ? errno : (n == 0 ? EPIPE : EPROTO)); break; }
            if (status.kind == FAILED) {
                fprintf(stderr, "POCKETSNES_AUDIO worker-failed stage=%s errno=%d error=%s\n",
                        backend_stage(status.stage), status.error, strerror(status.error));
                begin_stop(status.error ? status.error : EIO); break;
            }
            if (status.kind == READY && client.phase == STARTING) {
                client.phase = RUNNING; client.stats.ready = 1;
                fprintf(stderr, "POCKETSNES_AUDIO ready=1 backend=auddec transport=%s\n",
                        client.ipc.pipe ? "pipe" : "seqpacket");
            } else if (status.kind == ACK && client.phase == RUNNING && client.stats.inflight &&
                       status.frames && status.frames == client.outstanding[0]) {
                --client.stats.inflight;
                memmove(client.outstanding, client.outstanding + 1,
                        client.stats.inflight * sizeof(client.outstanding[0]));
                client.outstanding[client.stats.inflight] = 0;
                client.stats.delivered_frames += status.frames;
                client.deadline = now + M8_PCM_ACK_TIMEOUT_MS;
            } else { begin_stop(EPROTO); break; }
        }
    }
    if ((client.phase == STARTING || (client.phase == RUNNING && client.stats.inflight)) &&
        now >= client.deadline) begin_stop(ETIMEDOUT);
    if (client.stats.worker > 0) {
        pid_t result = waitpid(client.stats.worker, NULL, WNOHANG);
        if (result == client.stats.worker || (result < 0 && errno == ECHILD)) {
            for (unsigned i = 0; i < client.stats.inflight; ++i)
                client.stats.dropped_frames += client.outstanding[i];
            memset(client.outstanding, 0, sizeof(client.outstanding));
            ipc_close(&client.ipc);
            client.stats.worker = -1; client.stats.ready = 0; client.stats.inflight = 0;
            client.phase = OFF;
        } else if (client.phase == TERMINATING && now >= client.deadline) {
            (void)kill(client.stats.worker, SIGKILL);
            client.phase = KILLING; client.deadline = now + 50;
        } else if (client.phase == KILLING && now >= client.deadline) {
            client.phase = PARKED; /* Retain uninterruptible child's PID. */
        }
    }
}

int m8_pcm_requested(void) {
    const char *value = getenv("LILSMOKE_M8_AUDIO");
    return value && !strcmp(value, "1");
}

static int init_failure(const char *stage, int error) {
    client.stats.error = error ? error : EIO;
    client.reported = 0;
    fprintf(stderr, "POCKETSNES_AUDIO init-failed stage=%s errno=%d error=%s native-disabled=1\n",
            stage, client.stats.error, strerror(client.stats.error));
    errno = client.stats.error;
    return 0;
}

int m8_pcm_init(unsigned rate, unsigned channels, unsigned bits) {
    service();
    if (!m8_pcm_requested()) return 0;
    fprintf(stderr, "POCKETSNES_AUDIO init-request rate=%u channels=%u bits=%u\n", rate, channels, bits);
    if (rate != M8_PCM_RATE || channels != 2 || bits != 16) return init_failure("pcm-format", EINVAL);
    if (client.stats.worker > 0) return client.phase == STARTING || client.phase == RUNNING;
    int volume = 25;
    const char *setting = getenv("LILSMOKE_M8_VOLUME");
    if (setting) {
        char *end;
        errno = 0;
        long value = strtol(setting, &end, 10);
        if (!*setting || *end || errno || value < 0 || value > 50) return init_failure("volume", EINVAL);
        volume = (int)value;
    }
    struct pcm_ipc parent_ipc = {-1, -1, 0, 0, {0}};
    struct pcm_ipc child_ipc = {-1, -1, 0, 0, {0}};
    const char *stage;
    int fds[256];
    unsigned count = 0;
    if (ipc_pair(&parent_ipc, &child_ipc, &stage) < 0) goto fail;
    stage = "fd-enumeration";
    DIR *directory = opendir("/proc/self/fd");
    if (!directory) goto fail;
    struct dirent *entry;
    for (;;) {
        errno = 0;
        entry = readdir(directory);
        if (!entry) {
            int error = errno;
            (void)closedir(directory);
            if (error) { errno = error; goto fail; }
            break;
        }
        char *end;
        long number = strtol(entry->d_name, &end, 10);
        if (!*entry->d_name || *end || number < 0 || number > INT_MAX) continue;
        if (count == 256) { closedir(directory); errno = EMFILE; goto fail; }
        fds[count++] = (int)number;
    }
    pid_t parent = getpid();
    stage = "fork";
    pid_t child = fork();
    if (child < 0) goto fail;
    if (!child) worker(&child_ipc, parent, fds, count, volume);
    ipc_close(&child_ipc);
    memset(&client.stats, 0, sizeof(client.stats));
    client.stats.worker = child;
    client.ipc = parent_ipc; client.phase = STARTING;
    client.stats.transport = parent_ipc.pipe ? M8_PCM_IPC_PIPE : M8_PCM_IPC_SEQPACKET;
    client.reported = 0;
    memset(client.outstanding, 0, sizeof(client.outstanding));
    client.deadline = milliseconds() + 500;
    fprintf(stderr, "POCKETSNES_AUDIO driver=m8-native worker=%ld transport=%s rate=44100 stereo=1 hw_volume=%d queue_packets=%u\n",
            (long)child, parent_ipc.pipe ? "pipe" : "seqpacket", volume, (unsigned)M8_PCM_INFLIGHT_LIMIT);
    return 1;
fail:
    { int error = errno; ipc_close(&parent_ipc); ipc_close(&child_ipc); return init_failure(stage, error); }
}

int m8_pcm_active(void) { service(); return client.phase == STARTING || client.phase == RUNNING; }

int m8_pcm_send(const int16_t *samples, size_t frames) {
    service();
    if (!frames) return 1;
    if (!samples || frames > M8_PCM_CHUNK_FRAMES) { errno = EINVAL; return -1; }
    if (client.phase != RUNNING || client.stats.inflight >= M8_PCM_INFLIGHT_LIMIT) {
        client.stats.dropped_frames += frames; return 0;
    }
    struct pcm_packet packet;
    memset(&packet, 0, sizeof(packet));
    packet.frames = (uint32_t)frames;
    memcpy(packet.pcm, samples, frames * 4);
    size_t bytes = client.ipc.pipe ? sizeof(packet) : 4 + frames * 4;
    ssize_t n = ipc_send(&client.ipc, &packet, bytes);
    if (n != (ssize_t)bytes) {
        if (n >= 0 || (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR))
            begin_stop(n >= 0 ? EPROTO : errno);
        client.stats.dropped_frames += frames; return 0;
    }
    if (!client.stats.inflight) client.deadline = milliseconds() + M8_PCM_ACK_TIMEOUT_MS;
    client.outstanding[client.stats.inflight++] = (uint32_t)frames;
    client.stats.submitted_frames += frames;
    return 1;
}

void m8_pcm_get_stats(struct m8_pcm_stats *stats) { service(); if (stats) *stats = client.stats; }

int m8_pcm_close(void) {
    service();
    begin_stop(0);
    for (unsigned i = 0; i < 24 && client.stats.worker > 0; ++i) {
        service();
        if (client.stats.worker <= 0) break;
        struct timespec delay = {0, 5000000};
        (void)nanosleep(&delay, NULL);
    }
    service();
    if (client.stats.worker > 0 && client.phase == TERMINATING) {
        (void)kill(client.stats.worker, SIGKILL); client.phase = PARKED;
    }
    if (!client.reported) {
        fprintf(stderr, "POCKETSNES_AUDIO stats submitted_frames=%llu delivered_frames=%llu dropped_frames=%llu errno=%d worker_pending=%d\n",
                (unsigned long long)client.stats.submitted_frames,
                (unsigned long long)client.stats.delivered_frames,
                (unsigned long long)client.stats.dropped_frames,
                client.stats.error, client.stats.worker > 0);
        client.reported = 1;
    }
    return client.stats.worker > 0 ? -1 : 0;
}
