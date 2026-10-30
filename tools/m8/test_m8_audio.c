#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static int opens, closes, inits, starts, flushes, volumes, writes;
static int regular_device, open_error, stat_error, init_error, start_error;
static int size_error, percent_error, volume_error, clock_error;
static int percent = 0, device_size = 65536;
static uint64_t mock_ms;
static ssize_t write_results[32];
static int write_errors[32], result_count, result_index;
static unsigned char seen[4096];
static size_t seen_bytes;

static int mock_open(const char *path, int flags, ...) {
    assert(flags & O_NONBLOCK);
    assert(flags & O_CLOEXEC);
    assert(flags & O_NOFOLLOW);
    ++opens;
    if (open_error) { errno = open_error; return -1; }
    if (!strcmp(path, "/dev/auddec")) { assert((flags & O_ACCMODE) == O_RDWR); return 0; }
    if (!strcmp(path, "/dev/sndC0i2so")) { assert((flags & O_ACCMODE) == O_WRONLY); return 7; }
    assert(0); return -1;
}
static int mock_close(int fd) { assert(fd == 0 || fd == 7); ++closes; return 0; }
static int mock_fstat(int fd, struct stat *st) {
    assert(fd == 0 || fd == 7);
    if (stat_error) { errno = stat_error; return -1; }
    memset(st, 0, sizeof(*st));
    st->st_mode = regular_device ? S_IFREG : S_IFCHR;
    return 0;
}
static int mock_clock_gettime(clockid_t clock, struct timespec *ts) {
    assert(clock == CLOCK_MONOTONIC);
    if (clock_error) { errno = clock_error; return -1; }
    ts->tv_sec = (time_t)(mock_ms / 1000);
    ts->tv_nsec = (long)(mock_ms % 1000) * 1000000;
    return 0;
}
static int mock_ioctl(int fd, unsigned long request, ...);
static ssize_t mock_write(int fd, const void *buffer, size_t bytes) {
    assert(fd == 0);
    ++writes;
    if (result_index < result_count) {
        int i = result_index++;
        errno = write_errors[i];
        if (write_results[i] <= 0) return write_results[i];
        assert((size_t)write_results[i] <= bytes);
        return write_results[i];
    }
    assert(seen_bytes + bytes <= sizeof(seen));
    memcpy(seen + seen_bytes, buffer, bytes);
    seen_bytes += bytes;
    return (ssize_t)bytes;
}

#define M8_AUDIO_TEST
#define open mock_open
#define close mock_close
#define fstat mock_fstat
#define ioctl mock_ioctl
#define write mock_write
#define clock_gettime mock_clock_gettime
#include "../../board/segam-m8-v6-stock/m8-audio.c"
#undef open
#undef close
#undef fstat
#undef ioctl
#undef write
#undef clock_gettime

static int mock_ioctl(int fd, unsigned long request, ...) {
    va_list args;
    int error = 0;
    va_start(args, request);
    if (request == M8_AUDDEC_INIT) {
        _Alignas(8) unsigned char expected[624] = {0};
        unsigned char *cfg = va_arg(args, void *);
        assert(fd == 0 && ((uintptr_t)cfg % 8) == 0);
        /* Literal expected bytes, independent of the backend's encoder. */
        expected[2] = 16; expected[3] = 2;
        expected[6] = 1;
        expected[12] = 0x44; expected[13] = 0xac;
        expected[24] = 1; expected[28] = 100; expected[580] = 100;
        assert(!memcmp(cfg, expected, sizeof(expected)));
        ++inits; error = init_error;
    } else if (request == M8_AUDDEC_START) {
        assert(fd == 0 && va_arg(args, unsigned long) == 0);
        ++starts; error = start_error;
    } else if (request == M8_BUFFER_SIZE) {
        assert(fd == 0);
        *va_arg(args, int *) = device_size; error = size_error;
    } else if (request == M8_BUFFER_PERCENT) {
        assert(fd == 0);
        if (!percent_error) *va_arg(args, int *) = percent;
        error = percent_error;
    } else if (request == M8_SND_SET_VOLUME) {
        assert(fd == 7 && *va_arg(args, uint8_t *) <= 50);
        ++volumes; error = volume_error;
    } else if (request == M8_AUDDEC_FLUSH) {
        assert(fd == 0 && va_arg(args, unsigned long) == 0); ++flushes;
    } else assert(0);
    va_end(args);
    if (error) { errno = error; return -1; }
    return 0;
}

static void reset(struct m8_audio *a) {
    opens = closes = inits = starts = flushes = volumes = writes = 0;
    regular_device = open_error = stat_error = init_error = start_error = 0;
    size_error = percent_error = volume_error = clock_error = 0;
    percent = 0; device_size = 65536; mock_ms = 0;
    result_count = result_index = 0; seen_bytes = 0;
    memset(write_results, 0, sizeof(write_results));
    memset(write_errors, 0, sizeof(write_errors));
    m8_audio_init(a);
    assert(setenv("LILSMOKE_M8_AUDIO", "1", 1) == 0);
}

int main(void) {
    struct m8_audio a;
    unsigned char pcm[M8_AUDIO_MAX_BYTES];
    memset(pcm, 0x5a, sizeof(pcm));

    reset(&a); unsetenv("LILSMOKE_M8_AUDIO");
    assert(m8_audio_open(&a, -1) == -1 && errno == EPERM && opens == 0);
    setenv("LILSMOKE_M8_AUDIO", "1", 1);
    assert(m8_audio_open(&a, -1) == -1 && opens == 0);
    reset(&a); setenv("LILSMOKE_M8_AUDIO", "yes", 1);
    assert(m8_audio_open(&a, -1) == -1 && opens == 0);
    reset(&a); assert(m8_audio_open(&a, 51) == -1 && opens == 0);
    reset(&a); assert(m8_audio_open(&a, -2) == -1 && opens == 0);
    reset(&a); open_error = ENOENT;
    assert(m8_audio_open(&a, -1) == -1 && errno == ENOENT && closes == 0);
    reset(&a); regular_device = 1;
    assert(m8_audio_open(&a, -1) == -1 && errno == ENODEV && inits == 0 && closes == 1);
    reset(&a); stat_error = EIO;
    assert(m8_audio_open(&a, -1) == -1 && errno == EIO && closes == 1);
    reset(&a); init_error = ENOTTY;
    assert(m8_audio_open(&a, -1) == -1 && starts == 0 && closes == 1);
    reset(&a); start_error = EIO;
    assert(m8_audio_open(&a, -1) == -1 && closes == 1);
    reset(&a); size_error = EIO;
    assert(m8_audio_open(&a, -1) == -1 && closes == 1);
    reset(&a); device_size = 0;
    assert(m8_audio_open(&a, -1) == -1 && errno == EPROTO && closes == 1);
    reset(&a); device_size = 16 * 1024 * 1024 + 1;
    assert(m8_audio_open(&a, -1) == -1 && errno == EPROTO);
    reset(&a); volume_error = EIO;
    assert(m8_audio_open(&a, 20) == -1 && opens == 2 && closes == 2);
    reset(&a); assert(m8_audio_open(&a, 20) == 0 && volumes == 1 && closes == 1);
    m8_audio_close(&a); assert(closes == 2);

    reset(&a); assert(m8_audio_open(&a, -1) == 0);
    assert(m8_audio_open(&a, -1) == -1 && errno == EALREADY && opens == 1);
    assert(m8_audio_submit(&a, NULL, 0) == 1 && writes == 0);
    assert(m8_audio_submit(&a, NULL, 1) == -1 && errno == EINVAL && !a.failed);
    assert(m8_audio_submit(&a, pcm, SIZE_MAX) == -1 && errno == EINVAL);
    percent = 91;
    assert(m8_audio_submit(&a, pcm, 735) == 0 && writes == 0);
    percent = 90;
    assert(m8_audio_submit(&a, pcm, 735) == 1);
    assert(m8_audio_submit(&a, pcm, 1) == 0);
    memset(pcm, 0x33, sizeof(pcm));
    assert(m8_audio_pump(&a) == 1 && writes == 2);
    const unsigned char header[16] = {255,255,255,255,0,0,0,0,0x7c,0x0b,0,0,0,0,0,0};
    assert(!memcmp(seen, header, 16));
    assert(seen_bytes == 16 + 2940 && seen[16] == 0x5a && seen[2955] == 0x5a);
    assert(a.accepted_frames == 735 && a.delivered_frames == 735);
    assert(m8_audio_pump(&a) == 1 && writes == 2);
    assert(m8_audio_flush(&a) == 0 && flushes == 1);
    m8_audio_close(&a); m8_audio_close(&a); assert(closes == 1 && flushes == 2);

    for (int bad = -1; bad <= 101; bad += 102) {
        reset(&a); assert(m8_audio_open(&a, -1) == 0); percent = bad;
        assert(m8_audio_submit(&a, pcm, 1) == -1 && errno == EPROTO && writes == 0);
    }
    reset(&a); assert(m8_audio_open(&a, -1) == 0); percent_error = EIO;
    assert(m8_audio_submit(&a, pcm, 1) == -1 && errno == EIO && writes == 0);
    reset(&a); assert(m8_audio_open(&a, -1) == 0); clock_error = EIO;
    assert(m8_audio_submit(&a, pcm, 1) == -1 && errno == EIO && writes == 0);

    reset(&a); assert(m8_audio_open(&a, -1) == 0);
    assert(m8_audio_submit(&a, pcm, 1) == 1);
    result_count = 1; write_results[0] = -1; write_errors[0] = EINTR;
    assert(m8_audio_pump(&a) == 0 && a.phase == M8_HEADER);
    assert(m8_audio_pump(&a) == 1 && writes == 3 && seen_bytes == 20);
    m8_audio_close(&a);

    reset(&a); assert(m8_audio_open(&a, -1) == 0);
    assert(m8_audio_submit(&a, pcm, 1) == 1);
    /* Whole header accepted; payload EAGAIN then succeeds, with no second header. */
    result_count = 2; write_results[0] = 16; write_results[1] = -1; write_errors[1] = EAGAIN;
    assert(m8_audio_pump(&a) == 0 && a.phase == M8_PAYLOAD);
    assert(m8_audio_submit(&a, pcm, 1) == 0);
    assert(m8_audio_pump(&a) == 1 && writes == 3 && seen_bytes == 4);
    m8_audio_close(&a);

    for (int part = 0; part < 2; ++part) {
        reset(&a); assert(m8_audio_open(&a, -1) == 0);
        assert(m8_audio_submit(&a, pcm, 1) == 1);
        result_count = part + 1;
        if (part) write_results[0] = 16;
        write_results[part] = 1;
        assert(m8_audio_pump(&a) == -1 && errno == EPROTO);
        assert(closes == 1 && flushes == 1 && a.failed);
        assert(m8_audio_pump(&a) == -1 && writes == part + 1);
    }
    reset(&a); assert(m8_audio_open(&a, -1) == 0);
    assert(m8_audio_submit(&a, pcm, 1) == 1);
    result_count = 8;
    for (int i = 0; i < 8; ++i) { write_results[i] = -1; write_errors[i] = EAGAIN; }
    for (int i = 0; i < 7; ++i) assert(m8_audio_pump(&a) == 0);
    assert(m8_audio_pump(&a) == -1 && errno == ETIMEDOUT && writes == 8 && flushes == 1);
    reset(&a); assert(m8_audio_open(&a, -1) == 0);
    assert(m8_audio_submit(&a, pcm, 1) == 1); mock_ms = 100;
    assert(m8_audio_pump(&a) == -1 && errno == ETIMEDOUT && writes == 0);
    reset(&a); assert(m8_audio_open(&a, -1) == 0);
    assert(m8_audio_submit(&a, pcm, 1) == 1);
    result_count = 1; write_results[0] = -1; write_errors[0] = ENODEV;
    assert(m8_audio_pump(&a) == -1 && errno == ENODEV && closes == 1);
    reset(&a); assert(m8_audio_open(&a, -1) == 0);
    assert(m8_audio_submit(&a, pcm, 1) == 1); result_count = 1;
    assert(m8_audio_pump(&a) == 0); /* A zero write is a bounded stall. */
    assert(m8_audio_flush(&a) == 0 && a.phase == M8_IDLE);
    m8_audio_close(&a);

    printf("PASS: exact 624-byte MIPS config, opt-in, character nodes, fd 0, volume cap, packet ownership, backpressure, bounded stalls, short-write fail-closed. State=%zu bytes. No hardware accessed.\n", sizeof(a));
    return 0;
}
