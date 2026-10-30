#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "audio/audio_driver.h"
#include "verbosity.h"
#include "m8_pocketsnes_audio.h"

static int requested = 1, initialize_ok = 1, active = 1;
static unsigned initializations, shutdowns, packets;
static struct m8_pcm_stats stats;
void RARCH_LOG(const char *fmt, ...) { (void)fmt; }
void RARCH_WARN(const char *fmt, ...) { (void)fmt; }
int m8_pcm_requested(void) { return requested; }
int m8_pcm_init(unsigned rate, unsigned channels, unsigned bits) {
    assert(rate == 44100 && channels == 2 && bits == 16);
    ++initializations; stats.inflight = 0; return initialize_ok;
}
int m8_pcm_active(void) { return active; }
int m8_pcm_send(const int16_t *pcm, size_t frames) {
    assert(frames == 735);
    for (size_t i = 0; i < frames * 2; ++i) assert(pcm[i] == (i & 1 ? -1234 : 1234));
    ++packets;
    if (stats.inflight == 2) { stats.dropped_frames += frames; return 0; }
    ++stats.inflight; stats.submitted_frames += frames; return 1;
}
void m8_pcm_get_stats(struct m8_pcm_stats *out) { *out = stats; }
int m8_pcm_close(void) { ++shutdowns; stats.inflight = 0; return 0; }

#include "../../package/retroarch-mips/m8_lilsmoke_audio.c"

int main(void) {
    unsigned rate = 0;
    stats.worker = -1;
    requested = 0;
    assert(!audio_lilsmoke.init(NULL, 48000, 64, 0, &rate) && rate == 44100);
    assert(!initializations);
    requested = 1;
    void *data = audio_lilsmoke.init(NULL, 48000, 64, 0, &rate);
    assert(data && rate == 44100 && initializations == 1);
    assert(!strcmp(audio_lilsmoke.ident, "lilsmoke") && !audio_lilsmoke.use_float(data));
    assert(audio_lilsmoke.alive(data) && audio_lilsmoke.buffer_size(data) == 8820);
    assert(audio_lilsmoke.write_avail(data) == 8820);
    assert(!audio_lilsmoke.init(NULL, 44100, 0, 0, &rate) && errno == EBUSY);

    int16_t pcm[735 * 2 * 8];
    for (size_t i = 0; i < sizeof(pcm) / sizeof(pcm[0]); ++i) pcm[i] = i & 1 ? -1234 : 1234;
    const unsigned char *bytes = (const unsigned char *)pcm;
    assert(audio_lilsmoke.write(data, NULL, 0) == 0);
    assert(audio_lilsmoke.write(data, bytes, 1) == 1 && context.used == 1);
    assert(audio_lilsmoke.write(data, bytes + 1, 13) == 13 && context.used == 14);
    assert(audio_lilsmoke.write(data, bytes + 14, 2940 - 14) == 2940 - 14 && packets == 1);
    assert(!context.used && stats.inflight == 1 && audio_lilsmoke.write_avail(data) == 5880);
    assert(audio_lilsmoke.write(data, bytes, 2940) == 2940 && stats.inflight == 2);
    assert(audio_lilsmoke.write_avail(data) == 2940);
    audio_lilsmoke.set_nonblock_state(data, false);
    assert(audio_lilsmoke.write(data, bytes, 2940) == 2940 && packets == 3 && context.dropped_bytes == 2940);
    assert(stats.inflight == 2);

    unsigned before = packets;
    assert(audio_lilsmoke.write(data, bytes, M8_RA_WRITE_BUDGET + 2941) == M8_RA_WRITE_BUDGET + 2941);
    assert(packets - before == 8 && !context.used && context.skip == 3);
    assert(audio_lilsmoke.write(data, bytes, 3) == 3 && !context.skip);
    assert(audio_lilsmoke.write(data, NULL, 1) == -1 && errno == EINVAL);
    assert(audio_lilsmoke.write(data, bytes, (size_t)SSIZE_MAX + 1) == -1 && errno == EINVAL);
    assert(audio_lilsmoke.write(NULL, bytes, 4) == -1 && errno == EINVAL);
    assert(audio_lilsmoke.write(data, bytes, 33) == 33 && context.used == 33);
    assert(audio_lilsmoke.stop(data) && !context.used && !audio_lilsmoke.alive(data));
    assert(audio_lilsmoke.write_avail(data) == 0 && shutdowns == 1);
    before = packets;
    assert(audio_lilsmoke.write(data, bytes, 2940) == 2940 && packets == before);
    assert(audio_lilsmoke.start(data, false) && initializations == 2 && audio_lilsmoke.alive(data));
    assert(audio_lilsmoke.write(data, bytes, 2940) == 2940 && packets == before + 1);
    active = 0;
    assert(audio_lilsmoke.write(data, bytes, 2940) == 2940 && context.silent && audio_lilsmoke.alive(data));
    assert(audio_lilsmoke.write_avail(data) == 8820);
    before = packets;
    assert(audio_lilsmoke.write(data, bytes, 2940) == 2940 && packets == before);
    audio_lilsmoke.free(data); assert(!context.occupied && shutdowns == 2);
    audio_lilsmoke.free(data); audio_lilsmoke.free(NULL); assert(shutdowns == 2);
    initialize_ok = 0;
    data = audio_lilsmoke.init(NULL, 32000, 1000, 512, &rate);
    assert(data && rate == 44100 && context.silent);
    assert(audio_lilsmoke.write(data, bytes, 2940) == 2940 && packets == before);
    audio_lilsmoke.free(data);
    printf("PASS: real RA audio_driver_t ABI, forced 44100 stereo S16, unaligned/sub-frame fragments, 735-frame batching, two-credit room, bounded huge writes/drop-consumed return, pause/resume, unavailable worker, no reopen loop, single owner and teardown. Context=%zu bytes. No device access.\n", sizeof(context));
    return 0;
}
