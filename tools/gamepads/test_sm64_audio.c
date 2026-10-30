#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "audio_resample.h"

struct capture { int16_t *samples; size_t frames, capacity; };
static void capture(void *context, int16_t left, int16_t right) {
    struct capture *out = context;
    assert(out->frames < out->capacity);
    out->samples[out->frames * 2] = left; out->samples[out->frames * 2 + 1] = right; ++out->frames;
}
int main(void) {
    const size_t frames = 320000, capacity = 441000;
    int16_t *input = malloc(frames * 4), *a = malloc(capacity * 4), *b = malloc(capacity * 4);
    assert(input && a && b);
    for (size_t i = 0; i < frames; ++i) { input[i * 2] = (int16_t)(i * 31); input[i * 2 + 1] = (int16_t)(i * 47); }
    struct capture whole = {a, 0, capacity}, pieces = {b, 0, capacity};
    struct sm64_resampler first = {0}, second = {0};
    sm64_resample(&first, input, frames, capture, &whole);
    for (size_t i = 0; i < frames;) {
        size_t count = i % 1088 + 1; if (count > frames - i) count = frames - i;
        sm64_resample(&second, input + i * 2, count, capture, &pieces); i += count;
    }
    assert(whole.frames == ((frames - 1) * 441 + 319) / 320 && whole.frames == pieces.frames);
    assert(!memcmp(a, b, whole.frames * 4));
    memset(&first, 0, sizeof(first)); whole.frames = 0;
    for (size_t i = 0; i < frames; ++i) { input[i * 2] = -32768; input[i * 2 + 1] = 32767; }
    sm64_resample(&first, input, frames, capture, &whole);
    for (size_t i = 0; i < whole.frames; ++i) assert(a[i * 2] == -32768 && a[i * 2 + 1] == 32767);
    free(input); free(a); free(b);
    puts("PASS stereo isolation, full signed range, exact 10-second rate, arbitrary chunk boundaries");
    return 0;
}
