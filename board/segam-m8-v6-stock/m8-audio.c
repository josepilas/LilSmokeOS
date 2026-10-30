#define _POSIX_C_SOURCE 200809L
#include "m8-audio.h"

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

/* Exact user driver.so SHA256 5b761f9a5370454737c2b716acfca421d
 * bb82c22112c5d4f3e6a8f604503d1a3, pcm_decoder_init at 0x63d0.
 * Do NOT substitute R36SX's 0x82780301 / 632-byte config, or compute the
 * request from the differently sized structure in current SDK headers.
 * MIPS ioctl encoding differs from x86; constants intentionally fixed. */
#define M8_AUDDEC_INIT 0x82700301UL
#define M8_AUDDEC_START 0x20000304UL
#define M8_AUDDEC_FLUSH 0x20000305UL
#define M8_BUFFER_PERCENT 0x40041502UL
#define M8_BUFFER_SIZE 0x40041503UL
#define M8_SND_SET_VOLUME 0x8001080bUL
#define M8_CONFIG_SIZE 624U
#ifndef M8_PENDING_LIMIT_MS
#define M8_PENDING_LIMIT_MS 100U
#endif
#ifndef M8_STALL_LIMIT
#define M8_STALL_LIMIT 8U
#endif

enum { M8_IDLE, M8_HEADER, M8_PAYLOAD };

#if defined(__mips__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
_Static_assert(sizeof(void *) == 4 && sizeof(long) == 4, "M8 requires o32");
_Static_assert(_IOW(3, 1, unsigned char[624]) == M8_AUDDEC_INIT, "MIPS init ABI");
_Static_assert(_IO(3, 4) == M8_AUDDEC_START, "MIPS start ABI");
_Static_assert(_IO(3, 5) == M8_AUDDEC_FLUSH, "MIPS flush ABI");
_Static_assert(_IOR(21, 2, int) == M8_BUFFER_PERCENT, "MIPS buffer ABI");
_Static_assert(_IOW(8, 11, uint8_t) == M8_SND_SET_VOLUME, "MIPS volume ABI");
#endif
_Static_assert(sizeof(struct m8_audio) <= 3072, "bounded PCM state");

static void put_le32(unsigned char *p, uint32_t value) {
    p[0] = (unsigned char)value;
    p[1] = (unsigned char)(value >> 8);
    p[2] = (unsigned char)(value >> 16);
    p[3] = (unsigned char)(value >> 24);
}

static int fail(struct m8_audio *audio, int error) {
    if (audio->fd >= 0) {
        /* Closing/flush discards an incomplete header/payload transaction. */
        if (audio->phase != M8_IDLE)
            (void)ioctl(audio->fd, M8_AUDDEC_FLUSH, 0UL);
        (void)close(audio->fd);
        audio->fd = -1;
    }
    audio->failed = 1;
    audio->last_errno = error ? error : EIO;
    audio->phase = M8_IDLE;
    audio->bytes = 0;
    errno = audio->last_errno;
    return -1;
}

static int char_open(const char *path, int flags) {
    struct stat st;
    int fd = open(path, flags | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) return -1;
    if (fstat(fd, &st) < 0) {
        int error = errno;
        (void)close(fd);
        errno = error;
        return -1;
    }
    if (!S_ISCHR(st.st_mode)) {
        (void)close(fd);
        errno = ENODEV;
        return -1;
    }
    return fd;
}

static int now_ms(uint64_t *value) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) < 0) return -1;
    *value = (uint64_t)ts.tv_sec * 1000U + (uint64_t)ts.tv_nsec / 1000000U;
    return 0;
}

void m8_audio_init(struct m8_audio *audio) {
    memset(audio, 0, sizeof(*audio));
    audio->fd = -1;
}

int m8_audio_open(struct m8_audio *audio, int hardware_volume) {
    const char *setting = getenv("LILSMOKE_M8_AUDIO");
    _Alignas(8) unsigned char config[M8_CONFIG_SIZE];
    int buffer_size = 0;
    if (audio->fd >= 0) { errno = EALREADY; return -1; }
    if (audio->failed) { errno = audio->last_errno; return -1; }
    if (!setting || strcmp(setting, "1")) return fail(audio, EPERM);
#if !defined(M8_AUDIO_TEST) && (!defined(__mips__) || __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__)
    return fail(audio, ENOTSUP);
#endif
    if (hardware_volume < -1 || hardware_volume > 50)
        return fail(audio, EINVAL);
    audio->fd = char_open("/dev/auddec", O_RDWR);
    if (audio->fd < 0) return fail(audio, errno);
    memset(config, 0, sizeof(config));
    config[2] = 16;
    config[3] = M8_AUDIO_CHANNELS;
    put_le32(config + 4, 0x10000U); /* HC_AVCODEC_ID_PCM_S16LE */
    put_le32(config + 12, M8_AUDIO_RATE);
    put_le32(config + 24, 1); /* Stock snd_devs: I2SO only. */
    put_le32(config + 28, 100);
    put_le32(config + 576, 0);
    put_le32(config + 580, 100);
    /* All other bytes, including block_align and unknown tail, match stock. */
    if (ioctl(audio->fd, M8_AUDDEC_INIT, config) != 0 ||
        ioctl(audio->fd, M8_AUDDEC_START, 0UL) != 0 ||
        ioctl(audio->fd, M8_BUFFER_SIZE, &buffer_size) != 0)
        return fail(audio, errno);
    if (buffer_size < (int)(M8_AUDIO_MAX_BYTES + 16U) ||
        buffer_size > 16 * 1024 * 1024)
        return fail(audio, EPROTO);
    if (hardware_volume >= 0) {
        uint8_t volume = (uint8_t)hardware_volume;
        int fd = char_open("/dev/sndC0i2so", O_WRONLY);
        if (fd < 0) return fail(audio, errno);
        int result = ioctl(fd, M8_SND_SET_VOLUME, &volume);
        int error = errno;
        (void)close(fd);
        if (result != 0) return fail(audio, error);
    }
    return 0;
}

static int write_part(struct m8_audio *audio, const void *data, size_t bytes) {
    ssize_t result = write(audio->fd, data, bytes);
    if (result == (ssize_t)bytes) return 1;
    /* Stock retries the whole object, not a byte-stream suffix. Do not guess
     * what a partial write means for this packet device; disable on anomalies. */
    if (result > 0) return fail(audio, EPROTO);
    if (result < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)
        return fail(audio, errno);
    if (++audio->stalls >= M8_STALL_LIMIT) return fail(audio, ETIMEDOUT);
    return 0;
}

int m8_audio_pump(struct m8_audio *audio) {
    uint64_t now;
    int result;
    if (audio->failed) { errno = audio->last_errno; return -1; }
    if (audio->fd < 0) { errno = EBADF; return -1; }
    if (audio->phase == M8_IDLE) return 1;
    if (now_ms(&now) < 0) return fail(audio, errno);
    if (now >= audio->deadline_ms) return fail(audio, ETIMEDOUT);
    if (audio->phase == M8_HEADER) {
        result = write_part(audio, audio->header, sizeof(audio->header));
        if (result <= 0) return result;
        audio->phase = M8_PAYLOAD;
    }
    result = write_part(audio, audio->pcm, audio->bytes);
    if (result <= 0) return result;
    audio->delivered_frames += audio->bytes / 4U;
    audio->phase = M8_IDLE;
    audio->bytes = 0;
    return 1;
}

int m8_audio_submit(struct m8_audio *audio, const void *pcm, size_t frames) {
    int percent = -1;
    uint64_t now;
    if (audio->failed) { errno = audio->last_errno; return -1; }
    if (audio->fd < 0) { errno = EBADF; return -1; }
    if (!frames) return 1;
    if (!pcm || frames > M8_AUDIO_MAX_FRAMES) { errno = EINVAL; return -1; }
    if (audio->phase != M8_IDLE) { ++audio->busy_packets; return 0; }
    if (ioctl(audio->fd, M8_BUFFER_PERCENT, &percent) != 0)
        return fail(audio, errno);
    if (percent < 0 || percent > 100) return fail(audio, EPROTO);
    if (percent >= 91) { ++audio->busy_packets; return 0; }
    if (now_ms(&now) < 0) return fail(audio, errno);
    audio->bytes = frames * 4U;
    memcpy(audio->pcm, pcm, audio->bytes);
    memset(audio->header, 0, sizeof(audio->header));
    put_le32(audio->header, UINT32_MAX); /* PTS=-1, duration=0, ES_DATA=0 */
    put_le32(audio->header + 8, (uint32_t)audio->bytes);
    audio->phase = M8_HEADER;
    audio->stalls = 0;
    audio->deadline_ms = now + M8_PENDING_LIMIT_MS;
    audio->accepted_frames += frames;
    return 1;
}

int m8_audio_flush(struct m8_audio *audio) {
    if (audio->failed) { errno = audio->last_errno; return -1; }
    if (audio->fd < 0) { errno = EBADF; return -1; }
    if (ioctl(audio->fd, M8_AUDDEC_FLUSH, 0UL) != 0)
        return fail(audio, errno);
    audio->phase = M8_IDLE;
    audio->bytes = 0;
    audio->stalls = 0;
    return 0;
}

void m8_audio_close(struct m8_audio *audio) {
    if (audio->fd >= 0) {
        (void)ioctl(audio->fd, M8_AUDDEC_FLUSH, 0UL);
        (void)close(audio->fd);
        audio->fd = -1;
    }
    audio->phase = M8_IDLE;
    audio->bytes = 0;
}
