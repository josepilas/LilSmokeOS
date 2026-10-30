#define _POSIX_C_SOURCE 200809L
#include "m8-audio.h"

#include <errno.h>
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static volatile sig_atomic_t stopped;
static void stop(int sig) { (void)sig; stopped = 1; }

static int wait_ms(unsigned ms) {
    struct timespec delay = {(time_t)(ms / 1000), (long)(ms % 1000) * 1000000};
    return nanosleep(&delay, NULL);
}

static int send_chunk(struct m8_audio *audio, const void *pcm) {
    int result = 0;
    for (unsigned i = 0; i < 50 && !stopped; ++i) {
        result = m8_audio_submit(audio, pcm, M8_AUDIO_MAX_FRAMES);
        if (result != 0) break;
        if (wait_ms(2) < 0) return -1;
    }
    if (result <= 0 || stopped) { if (!result) errno = ETIMEDOUT; return -1; }
    for (unsigned i = 0; i < 8 && !stopped; ++i) {
        result = m8_audio_pump(audio);
        if (result != 0) return result < 0 ? -1 : 0;
        if (wait_ms(2) < 0) return -1;
    }
    errno = ETIMEDOUT;
    return -1;
}

int main(int argc, char **argv) {
    int volume = -1;
    int tone = 0, enabled = 0, describe = 0;
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--tone")) tone = 1;
        else if (!strcmp(argv[i], "--enable")) enabled = 1;
        else if (!strcmp(argv[i], "--describe")) describe = 1;
        else if (!strcmp(argv[i], "--volume") && i + 1 < argc) {
            char *end;
            const char *value = argv[++i];
            long n = strtol(value, &end, 10);
            if (!*value || *end || n < 0 || n > 50) goto usage;
            volume = (int)n;
        } else goto usage;
    }
    if (describe && !tone && !enabled && volume == -1) {
        puts("M8 stock audio: /dev/auddec, init=0x82700301, config=624 bytes, PCM S16LE stereo 44100 Hz, packet=16-byte header + <=2940-byte payload. Disabled without --tone --enable. No hardware accessed.");
        return 0;
    }
    if (!tone || !enabled || describe) goto usage;
    if (setenv("LILSMOKE_M8_AUDIO", "1", 1) < 0) { perror("M8Audio opt-in"); return 1; }
    (void)signal(SIGINT, stop);
    (void)signal(SIGTERM, stop);
    struct m8_audio audio;
    unsigned char pcm[M8_AUDIO_MAX_BYTES];
    m8_audio_init(&audio);
    if (m8_audio_open(&audio, volume) < 0) { perror("M8Audio open"); return 1; }
    fprintf(stderr, "M8Audio: one-second 440 Hz tone, peak 2048/32768, volume=%d (-1 means unchanged)\n", volume);
    int good = 1;
    for (unsigned chunk = 0; chunk < 60 && !stopped; ++chunk) {
        for (unsigned i = 0; i < M8_AUDIO_MAX_FRAMES; ++i) {
            unsigned sample = chunk * M8_AUDIO_MAX_FRAMES + i;
            unsigned edge = sample < M8_AUDIO_RATE - 1 - sample ? sample : M8_AUDIO_RATE - 1 - sample;
            float fade = edge < 2205 ? (float)edge / 2205.0f : 1.0f;
            int16_t value = (int16_t)(2048.0f * fade * sinf(6.28318530718f * 440.0f * (float)sample / (float)M8_AUDIO_RATE));
            for (unsigned channel = 0; channel < 2; ++channel) {
                size_t at = i * 4U + channel * 2U;
                pcm[at] = (unsigned char)(uint16_t)value;
                pcm[at + 1] = (unsigned char)((uint16_t)value >> 8);
            }
        }
        if (send_chunk(&audio, pcm) < 0) { perror("M8Audio packet"); good = 0; break; }
        /* Pace submission without relying on a vendor blocking write. */
        if (wait_ms(chunk % 3 == 2 ? 16 : 17) < 0) { good = 0; break; }
    }
    if (good && !stopped) (void)wait_ms(120);
    fprintf(stderr, "M8Audio: accepted=%llu delivered=%llu busy=%llu error=%d\n",
            (unsigned long long)audio.accepted_frames,
            (unsigned long long)audio.delivered_frames,
            (unsigned long long)audio.busy_packets, audio.last_errno);
    m8_audio_close(&audio);
    return good && !stopped ? 0 : 1;
usage:
    fputs("Usage: m8-audio-play --describe | --tone --enable [--volume 0..50]\nNo audio device is opened without --tone --enable.\n", stderr);
    return 2;
}
