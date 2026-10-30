#ifndef LILSMOKE_SM64_WORLD_SCALE_H
#define LILSMOKE_SM64_WORLD_SCALE_H
#include <stdint.h>
#include <string.h>

#ifdef CONVERT
typedef uint16_t sm64_scale_pixel;
#else
typedef uint32_t sm64_scale_pixel;
#endif

struct sm64_scale_axis { unsigned nearest, first, second, weight; };
struct sm64_world_scaler {
    unsigned width, height;
    struct sm64_scale_axis x[320], y[240];
};

static inline void sm64_scale_axis_init(struct sm64_scale_axis *map,
                                        unsigned source, unsigned destination) {
    for (unsigned i = 0; i < destination; ++i) {
        int center = (int)((2 * i + 1) * source * 32 / (2 * destination)) - 16;
        if (center < 0) center = 0;
        if (center > (int)((source - 1) * 32)) center = (source - 1) * 32;
        map[i] = (struct sm64_scale_axis){i * source / destination,
            (unsigned)center / 32, (unsigned)center / 32 + ((unsigned)center / 32 + 1 < source),
            (unsigned)center % 32};
    }
}

static inline void sm64_world_scaler_init(struct sm64_world_scaler *s, unsigned width) {
    s->width = width; s->height = width * 3 / 4;
    sm64_scale_axis_init(s->x, width, 320);
    sm64_scale_axis_init(s->y, s->height, 240);
}

/* Five fractional bits keep separated RGB565 channels within 32-bit arithmetic. */
static inline sm64_scale_pixel sm64_scale_mix(sm64_scale_pixel a, sm64_scale_pixel b, unsigned w) {
#ifdef CONVERT
    uint32_t rb = ((a & 0xf81fu) * (32 - w) + (b & 0xf81fu) * w) >> 5;
    uint32_t g = ((a & 0x07e0u) * (32 - w) + (b & 0x07e0u) * w) >> 5;
    return (rb & 0xf81fu) | (g & 0x07e0u);
#else
    uint32_t rb = ((a & 0x00ff00ffu) * (32 - w) + (b & 0x00ff00ffu) * w) >> 5;
    uint32_t g = ((a & 0x0000ff00u) * (32 - w) + (b & 0x0000ff00u) * w) >> 5;
    uint32_t alpha = (((a >> 24) * (32 - w) + (b >> 24) * w) >> 5) << 24;
    return (rb & 0x00ff00ffu) | (g & 0x0000ff00u) | alpha;
#endif
}

static inline void sm64_world_scale_smooth(const struct sm64_world_scaler *s,
        const sm64_scale_pixel *source, sm64_scale_pixel *destination) {
    sm64_scale_pixel rows[2][320];
    unsigned cached[2] = {UINT32_MAX, UINT32_MAX};
    for (unsigned y = 0; y < 240; ++y) {
        const struct sm64_scale_axis *v = &s->y[y];
        unsigned needed[2] = {v->first, v->second};
        unsigned count = v->weight ? 2 : 1;
        for (unsigned n = 0; n < count; ++n) {
            unsigned index = needed[n], slot = index & 1;
            if (cached[slot] == index) continue;
            const sm64_scale_pixel *in = source + index * s->width;
            for (unsigned x = 0; x < 320; ++x) {
                const struct sm64_scale_axis *m = &s->x[x];
                rows[slot][x] = sm64_scale_mix(in[m->first], in[m->second], m->weight);
            }
            cached[slot] = index;
        }
        sm64_scale_pixel *out = destination + y * 320;
        const sm64_scale_pixel *top = rows[v->first & 1];
        const sm64_scale_pixel *bottom = rows[v->second & 1];
        if (!v->weight) memcpy(out, top, 320 * sizeof(*out));
        else for (unsigned x = 0; x < 320; ++x)
            out[x] = sm64_scale_mix(top[x], bottom[x], v->weight);
    }
}

static inline void sm64_world_scale(const struct sm64_world_scaler *s,
                                    const sm64_scale_pixel *source, sm64_scale_pixel *destination,
                                    int smooth) {
    if (s->width == 320) {
        memcpy(destination, source, 320 * 240 * sizeof(*destination));
        return;
    }
    if (smooth) {
        /* Adjacent output rows share horizontal taps. Keep only two cached
         * rows; interpolation order and all rounding stay bit-identical. */
        sm64_world_scale_smooth(s, source, destination);
        return;
    }
    for (unsigned y = 0; y < 240; ++y) {
        sm64_scale_pixel *row = destination + y * 320;
        if (y && s->y[y].nearest == s->y[y - 1].nearest) {
            memcpy(row, row - 320, 320 * sizeof(*row));
            continue;
        }
        const sm64_scale_pixel *in = source + s->y[y].nearest * s->width;
        if (s->width == 256) {
            for (unsigned x = 0, i = 0; x < 320; x += 5, i += 4) {
                row[x] = row[x + 1] = in[i]; row[x + 2] = in[i + 1];
                row[x + 3] = in[i + 2]; row[x + 4] = in[i + 3];
            }
        } else if (s->width == 192) {
            for (unsigned x = 0, i = 0; x < 320; x += 5, i += 3) {
                row[x] = row[x + 1] = in[i]; row[x + 2] = row[x + 3] = in[i + 1];
                row[x + 4] = in[i + 2];
            }
        } else if (s->width == 160) {
            for (unsigned x = 0; x < 160; ++x) row[x * 2] = row[x * 2 + 1] = in[x];
        } else for (unsigned x = 0; x < 320; ++x) row[x] = in[s->x[x].nearest];
    }
}
#endif
