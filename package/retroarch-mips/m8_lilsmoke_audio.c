/* GPL-3.0-or-later. RetroArch adapter for the authoritative isolated M8 PCM API. */
#include "audio/audio_driver.h"
#include "m8_pocketsnes_audio.h"
#include "verbosity.h"
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <string.h>

#define M8_RA_PACKET_BYTES (M8_PCM_CHUNK_FRAMES * 2U * sizeof(int16_t))
#define M8_RA_WRITE_BUDGET (8U * M8_RA_PACKET_BYTES)
#define M8_RA_BUFFER_BYTES ((M8_PCM_INFLIGHT_LIMIT + 1U) * M8_RA_PACKET_BYTES)

struct m8_ra_audio {
    int16_t pcm[M8_PCM_CHUNK_FRAMES * 2];
    size_t used, skip;
    uint64_t input_bytes, accepted_frames, dropped_bytes;
    bool occupied, running, silent, nonblock_reported;
};
static struct m8_ra_audio context;
_Static_assert(sizeof(context) < 4096, "bounded RetroArch PCM context");

static void discard_partial(struct m8_ra_audio *audio) {
    audio->dropped_bytes += audio->used;
    audio->used = audio->skip = 0;
}

static void *lilsmoke_audio_init(const char *device, unsigned rate, unsigned latency,
                               unsigned block_frames, unsigned *new_rate) {
    (void)device; (void)latency; (void)block_frames;
    if (new_rate) *new_rate = M8_PCM_RATE;
    if (!m8_pcm_requested()) {
        RARCH_WARN("[lilsmoke audio]: disabled; LILSMOKE_M8_AUDIO=1 required\n");
        return NULL;
    }
    if (context.occupied) { errno = EBUSY; return NULL; }
    memset(&context, 0, sizeof(context));
    context.occupied = context.running = true;
    context.silent = !m8_pcm_init(M8_PCM_RATE, M8_PCM_CHANNELS, 16);
    RARCH_LOG("[lilsmoke audio]: input_rate=%u output_rate=44100 stereo=S16LE nonblocking=1 packets=2 silent=%d\n",
              rate, context.silent);
    return &context;
}

static ssize_t lilsmoke_audio_write(void *data, const void *buffer, size_t bytes) {
    struct m8_ra_audio *audio = (struct m8_ra_audio *)data;
    const unsigned char *source = (const unsigned char *)buffer;
    size_t remaining = bytes, budget = M8_RA_WRITE_BUDGET;
    if (audio != &context || !audio->occupied || (!buffer && bytes) || bytes > (size_t)SSIZE_MAX) {
        errno = EINVAL; return -1;
    }
    audio->input_bytes += bytes;
    if (!bytes) return 0;
    if (!audio->running || audio->silent) {
        audio->dropped_bytes += bytes;
        return (ssize_t)bytes;
    }
    if (!m8_pcm_active()) {
        discard_partial(audio);
        audio->silent = true;
        audio->dropped_bytes += bytes;
        RARCH_WARN("[lilsmoke audio]: PCM worker unavailable; continuing silently, no reopen loop\n");
        return (ssize_t)bytes;
    }
    if (audio->skip) {
        size_t skip = remaining < audio->skip ? remaining : audio->skip;
        audio->dropped_bytes += skip; audio->skip -= skip;
        source += skip; remaining -= skip;
    }
    while (remaining && budget) {
        size_t take = M8_RA_PACKET_BYTES - audio->used;
        if (take > remaining) take = remaining;
        if (take > budget) take = budget;
        /* memcpy handles unaligned input and sub-frame writes on strict MIPS. */
        memcpy((unsigned char *)audio->pcm + audio->used, source, take);
        audio->used += take; source += take; remaining -= take; budget -= take;
        if (audio->used == M8_RA_PACKET_BYTES) {
            if (m8_pcm_send(audio->pcm, M8_PCM_CHUNK_FRAMES) == 1)
                audio->accepted_frames += M8_PCM_CHUNK_FRAMES;
            else audio->dropped_bytes += M8_RA_PACKET_BYTES;
            audio->used = 0;
        }
    }
    if (remaining) {
        size_t phase = (audio->used + remaining) % 4U;
        audio->dropped_bytes += audio->used + remaining;
        audio->used = 0;
        audio->skip = (4U - phase) % 4U;
    }
    /* RA 1.19.1 passes bytes. Report input consumed, including deliberate
     * drops, so overload/device failure can never stall its rendering loop. */
    return (ssize_t)bytes;
}

static bool lilsmoke_audio_stop(void *data) {
    struct m8_ra_audio *audio = (struct m8_ra_audio *)data;
    if (audio != &context || !audio->occupied) return false;
    if (audio->running) {
        discard_partial(audio);
        audio->running = false;
        if (m8_pcm_close() < 0)
            RARCH_WARN("[lilsmoke audio]: worker reap pending; replacement remains blocked\n");
    }
    return true;
}

static bool lilsmoke_audio_start(void *data, bool is_shutdown) {
    struct m8_ra_audio *audio = (struct m8_ra_audio *)data;
    (void)is_shutdown;
    if (audio != &context || !audio->occupied) return false;
    if (!audio->running) {
        discard_partial(audio);
        audio->silent = !m8_pcm_init(M8_PCM_RATE, M8_PCM_CHANNELS, 16);
        audio->running = true;
    }
    return true;
}

static bool lilsmoke_audio_alive(void *data) {
    struct m8_ra_audio *audio = (struct m8_ra_audio *)data;
    return audio == &context && audio->occupied && audio->running;
}
static void lilsmoke_audio_nonblock(void *data, bool nonblock) {
    struct m8_ra_audio *audio = (struct m8_ra_audio *)data;
    if (audio == &context && audio->occupied && !nonblock && !audio->nonblock_reported) {
        audio->nonblock_reported = true;
        RARCH_LOG("[lilsmoke audio]: blocking mode ignored; isolated queue stays nonblocking\n");
    }
}
static bool lilsmoke_audio_float(void *data) { (void)data; return false; }

static size_t lilsmoke_audio_avail(void *data) {
    struct m8_ra_audio *audio = (struct m8_ra_audio *)data;
    struct m8_pcm_stats stats;
    unsigned credits;
    if (audio != &context || !audio->occupied || !audio->running) return 0;
    if (audio->silent) return M8_RA_BUFFER_BYTES;
    m8_pcm_get_stats(&stats);
    credits = stats.inflight < M8_PCM_INFLIGHT_LIMIT ? M8_PCM_INFLIGHT_LIMIT - stats.inflight : 0;
    return credits * M8_RA_PACKET_BYTES + M8_RA_PACKET_BYTES - audio->used;
}
static size_t lilsmoke_audio_size(void *data) { return data == &context && context.occupied ? M8_RA_BUFFER_BYTES : 0; }

static void lilsmoke_audio_free(void *data) {
    struct m8_ra_audio *audio = (struct m8_ra_audio *)data;
    struct m8_pcm_stats stats;
    if (audio != &context || !audio->occupied) return;
    (void)lilsmoke_audio_stop(audio);
    m8_pcm_get_stats(&stats);
    RARCH_LOG("[lilsmoke audio]: input_bytes=%llu accepted_frames=%llu delivered_frames=%llu adapter_dropped_bytes=%llu bridge_dropped_frames=%llu error=%d\n",
              (unsigned long long)audio->input_bytes, (unsigned long long)audio->accepted_frames,
              (unsigned long long)stats.delivered_frames, (unsigned long long)audio->dropped_bytes,
              (unsigned long long)stats.dropped_frames, stats.error);
    memset(audio, 0, sizeof(*audio));
}

audio_driver_t audio_lilsmoke = {
    lilsmoke_audio_init, lilsmoke_audio_write, lilsmoke_audio_stop, lilsmoke_audio_start,
    lilsmoke_audio_alive, lilsmoke_audio_nonblock, lilsmoke_audio_free, lilsmoke_audio_float,
    "lilsmoke", NULL, NULL, lilsmoke_audio_avail, lilsmoke_audio_size
};
