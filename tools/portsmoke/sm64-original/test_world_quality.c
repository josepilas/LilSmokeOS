#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "world_quality.h"
#include "world_scale.h"

static void window(struct sm64_world_quality *q, unsigned work, unsigned raster) {
    for (unsigned i = 0; i < 30; ++i) sm64_quality_sample(q, work, raster);
}

static void quality_tests(void) {
    const unsigned sizes[] = {160, 192, 256, 320};
    for (unsigned i = 0; i < 4; ++i) {
        struct sm64_world_quality q = sm64_quality_init(sizes[i], 0);
        for (unsigned n = 0; n < 1000; ++n) {
            assert(sm64_quality_sample(&q, 1000000, 1000000) == sizes[i]);
            assert(sm64_quality_sample(&q, 100, 100) == sizes[i]);
        }
    }
    struct sm64_world_quality q = sm64_quality_init(256, 1);
    window(&q, 50000, 45000); window(&q, 50000, 45000);
    assert(q.width == 256);
    window(&q, 36000, 32000); assert(q.width == 256);
    window(&q, 50000, 10000); assert(q.width == 256);
    for (unsigned n = 0; n < 29; ++n) assert(sm64_quality_sample(&q, 40000, 35000) == 256);
    assert(sm64_quality_sample(&q, 40000, 35000) == 192);
    for (unsigned n = 0; n < 3; ++n) window(&q, 10000, 8000);
    assert(q.width == 192 && q.headroom_windows == 0);
    for (unsigned n = 0; n < 5; ++n) window(&q, 15000, 12000);
    assert(q.width == 192);
    window(&q, 15001, 12000); assert(q.headroom_windows == 0);
    for (unsigned n = 0; n < 6; ++n) window(&q, 15000, 12000);
    assert(q.width == 256);
    for (unsigned n = 0; n < 3; ++n) window(&q, 50000, 45000);
    assert(q.width == 256);
    window(&q, 50000, 45000); assert(q.width == 192);
    for (unsigned n = 0; n < 20; ++n) window(&q, 50000, 45000);
    assert(q.width == 192);
    for (unsigned n = 0; n < 30; ++n) {
        window(&q, 14000, 10000); window(&q, 16000, 12000);
        assert(q.width == 192);
    }
    q = sm64_quality_init(256, 2);
    for (unsigned n = 0; n < 2; ++n) window(&q, 10000, 8000);
    for (unsigned n = 0; n < 5; ++n) window(&q, 18000, 15000);
    assert(q.width == 256);
    /* A single peak rejects a window even when its average has headroom. */
    sm64_quality_sample(&q, 21000, 15000);
    for (unsigned n = 0; n < 29; ++n) sm64_quality_sample(&q, 100, 50);
    assert(q.width == 256 && !q.headroom_windows);
    for (unsigned n = 0; n < 6; ++n) window(&q, 18000, 15000);
    assert(q.width == 320);
    assert(sm64_quality_sample(&q, 30000, 10000) == 320);
    assert(sm64_quality_sample(&q, 30001, 10000) == 256);
    assert(q.cooldown == 6 && !q.samples);
    for (unsigned n = 0; n < 6; ++n) window(&q, 100, 50);
    assert(q.width == 256 && !q.headroom_windows);
    for (unsigned n = 0; n < 30; ++n) window(&q, 100000, 100000);
    assert(q.width == 256);
}

static uint32_t random_state = 1;
static uint32_t next_random(void) {
    random_state = random_state * 1664525u + 1013904223u;
    return random_state;
}

static sm64_scale_pixel reference_mix(sm64_scale_pixel a, sm64_scale_pixel b, unsigned w) {
    sm64_scale_pixel result = 0;
#ifdef CONVERT
    const unsigned shift[] = {0, 5, 11}, mask[] = {31, 63, 31};
#else
    const unsigned shift[] = {0, 8, 16, 24}, mask[] = {255, 255, 255, 255};
#endif
    for (unsigned c = 0; c < sizeof(shift) / sizeof(*shift); ++c) {
        unsigned value = ((((a >> shift[c]) & mask[c]) * (32 - w)) +
            (((b >> shift[c]) & mask[c]) * w)) / 32;
        result |= (sm64_scale_pixel)((uint32_t)value << shift[c]);
    }
    return result;
}

static void scaling_tests(void) {
    for (unsigned i = 0; i < 100000; ++i) {
        sm64_scale_pixel a = next_random(), b = next_random();
        for (unsigned w = 0; w <= 32; ++w) assert(sm64_scale_mix(a, b, w) == reference_mix(a, b, w));
    }
    const unsigned widths[] = {160, 192, 256, 320};
    for (unsigned size = 0; size < 4; ++size) {
        unsigned width = widths[size], height = width * 3 / 4;
        sm64_scale_pixel *source = malloc(width * height * sizeof(*source));
        sm64_scale_pixel *allocation = malloc((320 * 240 + 2) * sizeof(*allocation));
        assert(source && allocation);
        sm64_scale_pixel *output = allocation + 1;
        allocation[0] = allocation[320 * 240 + 1] = 123;
        for (unsigned i = 0; i < width * height; ++i) source[i] = next_random();
        struct sm64_world_scaler s;
        sm64_world_scaler_init(&s, width);
        for (unsigned axis = 0; axis < 2; ++axis) {
            unsigned input = axis ? height : width, count = axis ? 240 : 320;
            struct sm64_scale_axis *map = axis ? s.y : s.x;
            for (unsigned i = 0; i < count; ++i) {
                double center = ((i + 0.5) * input / count - 0.5) * 32;
                if (center < 0) center = 0;
                if (center > (input - 1) * 32) center = (input - 1) * 32;
                assert(map[i].first * 32 + map[i].weight == (unsigned)center);
                assert(map[i].nearest == i * input / count);
            }
        }
        for (int smooth = 0; smooth <= 1; ++smooth) {
            sm64_world_scale(&s, source, output, smooth);
            for (unsigned y = 0; y < 240; ++y) for (unsigned x = 0; x < 320; ++x) {
                assert(s.x[x].first < width && s.x[x].second < width);
                assert(s.y[y].first < height && s.y[y].second < height);
                sm64_scale_pixel expected;
                if (!smooth || width == 320) expected = source[(y * height / 240) * width + x * width / 320];
                else {
                    struct sm64_scale_axis a = s.x[x], b = s.y[y];
                    sm64_scale_pixel top = reference_mix(source[b.first * width + a.first], source[b.first * width + a.second], a.weight);
                    sm64_scale_pixel bottom = reference_mix(source[b.second * width + a.first], source[b.second * width + a.second], a.weight);
                    expected = reference_mix(top, bottom, b.weight);
                }
                assert(output[y * 320 + x] == expected);
            }
            assert(allocation[0] == 123 && allocation[320 * 240 + 1] == 123);
        }
        /* Constant color and corner-clamping must not introduce seams. */
        for (unsigned i = 0; i < width * height; ++i) source[i] = (sm64_scale_pixel)-1;
        sm64_world_scale(&s, source, output, 1);
        for (unsigned i = 0; i < 320 * 240; ++i) assert(output[i] == (sm64_scale_pixel)-1);
        free(source); free(allocation);
    }
}

int main(void) {
    quality_tests(); scaling_tests();
    puts("PASS fixed widths, adaptive warmup/cooldown/hysteresis/raster budget, 3300000 pixel mixes, four-size nearest/smooth bounds/identity");
    return 0;
}
