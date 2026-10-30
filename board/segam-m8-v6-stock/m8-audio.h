#ifndef LILSMOKE_M8_AUDIO_H
#define LILSMOKE_M8_AUDIO_H

#include <stddef.h>
#include <stdint.h>

#define M8_AUDIO_RATE 44100U
#define M8_AUDIO_CHANNELS 2U
#define M8_AUDIO_MAX_FRAMES 735U
#define M8_AUDIO_MAX_BYTES (M8_AUDIO_MAX_FRAMES * 4U)

/* One owner/thread per instance. Call init before open; close before reuse.
 * All samples are interleaved signed 16-bit little-endian stereo at 44100 Hz.
 * No allocation, threads, device access or environment changes in init. */
struct m8_audio {
    int fd;
    int failed;
    int last_errno;
    unsigned phase;
    unsigned stalls;
    size_t bytes;
    uint64_t deadline_ms;
    uint64_t accepted_frames;
    uint64_t delivered_frames;
    uint64_t busy_packets;
    unsigned char header[16];
    unsigned char pcm[M8_AUDIO_MAX_BYTES];
};

void m8_audio_init(struct m8_audio *audio);
/* Requires LILSMOKE_M8_AUDIO=1 and a little-endian MIPS o32 build.
 * hardware_volume: -1 leaves current volume unchanged; 0..50 explicitly sets
 * the stock I2SO volume. No mute/route, persistentmem, GPIO or clock calls. */
int m8_audio_open(struct m8_audio *audio, int hardware_volume);
/* submit: 1 = copied/accepted, 0 = busy (not accepted), -1 = error.
 * At most 735 frames. A zero-frame call is a no-op. Accepted != delivered.
 * A pending packet is retried only by pump, never overwritten by submit. */
int m8_audio_submit(struct m8_audio *audio, const void *pcm, size_t frames);
/* pump: 1 = idle/delivered, 0 = pending, -1 = failed. No sleeps or loops.
 * Call regularly, including when there is no new audio. A packet may remain
 * pending for at most 100 ms / 8 stalled calls. Never run on the render thread
 * when a stuck vendor kernel syscall must be isolated from the game. */
int m8_audio_pump(struct m8_audio *audio);
/* Discards pending data and flushes this decoder, not a drain/EOS wait. */
int m8_audio_flush(struct m8_audio *audio);
void m8_audio_close(struct m8_audio *audio);

#endif
