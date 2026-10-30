#ifndef LILSMOKE_SM64_AUDIO_RESAMPLE_H
#define LILSMOKE_SM64_AUDIO_RESAMPLE_H
#include <stddef.h>
#include <stdint.h>

struct sm64_resampler {
    int16_t previous[2];
    unsigned phase;
    int initialized;
};
typedef void (*sm64_sample_output)(void *, int16_t, int16_t);

/* 32000 -> 44100, exact rational phase, one input sample of lookahead. */
static inline void sm64_resample(struct sm64_resampler *state, const int16_t *input,
                                size_t frames, sm64_sample_output output, void *context) {
    for (size_t i = 0; i < frames; ++i) {
        int16_t left = input[i * 2], right = input[i * 2 + 1];
        if (state->initialized) {
            while (state->phase < 441) {
                int32_t a = state->previous[0] + ((int32_t)left - state->previous[0]) * (int32_t)state->phase / 441;
                int32_t b = state->previous[1] + ((int32_t)right - state->previous[1]) * (int32_t)state->phase / 441;
                output(context, (int16_t)a, (int16_t)b);
                state->phase += 320;
            }
            state->phase -= 441;
        } else state->initialized = 1;
        state->previous[0] = left; state->previous[1] = right;
    }
}
#endif
