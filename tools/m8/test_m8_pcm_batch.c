#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <SDL.h>
#include "m8_pocketsnes_audio.h"

static unsigned packets, mixed, closes;
static int ready = 1, drop;
int m8_pcm_requested(void) { return 1; }
int m8_pcm_init(unsigned rate, unsigned channels, unsigned bits) {
    assert(rate == 44100 && channels == 2 && bits == 16);
    return 1;
}
int m8_pcm_active(void) { return ready; }
int m8_pcm_close(void) { ++closes; return 0; }
int m8_pcm_send(const int16_t *pcm, size_t frames) {
    assert(frames == 735);
    for (size_t i = 0; i < frames * 2; ++i) assert(pcm[i] == (i & 1 ? -1234 : 1234));
    ++packets;
    return !drop;
}
void S9xMixSamples(uint8_t *buffer, int count) {
    int16_t *pcm = (int16_t *)buffer;
    assert(count > 0 && count <= 1470 && !(count & 1));
    for (int i = 0; i < count; ++i) pcm[i] = i & 1 ? -1234 : 1234;
    mixed += count / 2;
}
int SDL_OpenAudio(SDL_AudioSpec *desired, SDL_AudioSpec *obtained) {
    (void)desired; (void)obtained; assert(0); return -1;
}
void SDL_PauseAudio(int pause) { (void)pause; assert(0); }
void SDL_CloseAudio(void) { assert(0); }

#include "sal_sound.c"

int main(void) {
    assert(sal_AudioInit(44100, 16, 1, 60) == SAL_OK);
    sal_AudioSetVolume(31, 31);
    sal_AudioSetMuted(0);
    for (unsigned i = 0; i < 735; ++i) sal_AudioGenerate(1);
    assert(packets == 1 && mixed == 735 && !NativeFrames);
    for (unsigned i = 0; i < 50; ++i) sal_AudioGenerate(17);
    assert(packets == 2 && NativeFrames == 115 && mixed == 1585);
    sal_AudioPause(); sal_AudioResume();
    assert(!NativeFrames && closes == 1);
    sal_AudioGenerate(882); assert(packets == 3 && NativeFrames == 147);
    drop = 1; sal_AudioGenerate(588);
    assert(packets == 4 && !NativeFrames);
    sal_AudioGenerate(100); sal_AudioSetMuted(1); sal_AudioGenerate(1);
    assert(!NativeFrames);
    unsigned before = mixed;
    sal_AudioSetMuted(0); ready = 0; sal_AudioGenerate(100);
    assert(mixed == before && !NativeFrames);
    ready = 1; drop = 0; sal_AudioGenerate(735);
    assert(packets == 5 && !NativeFrames);
    sal_AudioClose(); assert(closes == 2);
    puts("PASS: scanline fragments batch into 735-frame packets; PAL carry, pause, mute, overload and unavailable sink stay bounded. No device access.");
    return 0;
}
