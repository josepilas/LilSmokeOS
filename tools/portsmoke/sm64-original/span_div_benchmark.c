/* SPDX-License-Identifier: MPL-2.0 */
#include <limits.h>
#define main endpoint_main
#include "endpoint_benchmark.c"
#undef main

int main(int argc, char **argv) {
    uint32_t random = 0x5eed1234u;
    unsigned checked = 0;
    for (int count = 1; count <= 16; ++count) {
        const int edge[] = {INT_MIN, INT_MIN + 1, -65537, -65536, -257, -256,
            -17, -16, -15, -1, 0, 1, 15, 16, 17, 255, 256, 65535, INT_MAX};
        for (unsigned i = 0; i < sizeof(edge) / sizeof(*edge); ++i) {
            assert(opt_span_div(edge[i], count) == edge[i] / count);
            ++checked;
        }
        for (int i = -65536; i <= 65536; ++i) {
            assert(opt_span_div(i, count) == i / count);
            ++checked;
        }
        for (unsigned i = 0; i < 100000; ++i) {
            random ^= random << 13; random ^= random >> 17; random ^= random << 5;
            const int value = (int32_t)random;
            assert(opt_span_div(value, count) == value / count);
            ++checked;
            const int multiple = (value / count) * count;
            for (int delta = -1; delta <= 1; ++delta) {
                if ((delta < 0 && multiple == INT_MIN) || (delta > 0 && multiple == INT_MAX)) continue;
                assert(opt_span_div(multiple + delta, count) == (multiple + delta) / count);
                ++checked;
            }
        }
    }
    printf("DIV_EQ cases=%u\n", checked);
    int status = endpoint_main(argc, argv);
    for (unsigned iteration = 0; iteration < 12000; ++iteration) {
        random ^= random << 13; random ^= random >> 17; random ^= random << 5;
        const int width = iteration % 31 == 0 ? 0 : iteration % 37 == 0 ? -1 : (int)(random % 320) + 1;
        float p[16]; Vector2 dp[16]; reset(p, dp, 0, iteration & 1);
        p[2] = (random & 65535) / 65535.0f;
        p[3] = 1.0f + (random & 255) / 256.0f;
        for (unsigned i = 0; i < 328; ++i) {
            const unsigned pattern = (iteration + i / 16) % 5;
            depth[i] = pattern == 0 ? 0 : pattern == 1 ? 65535 : pattern == 2 ? (uint16_t)(p[2] * 65535.0f) : (uint16_t)(random + i * 997);
        }
        texture.filter = (iteration >> 1) & 1;
        texture.wrap_mode_x = iteration % 3; texture.wrap_mode_y = (iteration / 3) % 3;
        shader.draw_flags = iteration % 3 == 0 ? 0 : iteration % 3 == 1 ? DRAW_BLEND : DRAW_BLEND_EDGE;
        z_offset = iteration % 3 == 0 ? -65535.0f : iteration % 3 == 1 ? 0.0f : 65535.0f;
        z_test = iteration & 1; z_write = (iteration >> 1) & 1;
        const opt_pix_t left = color[3], right = color[4 + (width > 0 ? width : 0)];
        const uint16_t zleft = depth[3], zright = depth[4 + (width > 0 ? width : 0)];
        scanners[iteration % 12](color + 4, depth + 4, width, p, dp, &texture);
        assert(color[3] == left && color[4 + (width > 0 ? width : 0)] == right);
        assert(depth[3] == zleft && depth[4 + (width > 0 ? width : 0)] == zright);
        hash(color, sizeof(color)); hash(depth, sizeof(depth)); hash(p, sizeof(p));
    }
    printf("FUZZ_EQ cases=12000 hash=%08x\n", digest);
    const unsigned repeats = argc > 1 ? strtoul(argv[1], NULL, 10) : 1000;
    texture.filter = false; texture.wrap_mode_x = texture.wrap_mode_y = WRAP_REPEAT;
    shader.draw_flags = 0; z_test = z_write = true; z_offset = 0;
    for (unsigned hidden = 0; hidden < 3; ++hidden) {
        const double start = seconds();
        for (unsigned iteration = 0; iteration < repeats; ++iteration)
        for (unsigned scan = 0; scan < 12; ++scan)
        for (int width = 1; width <= 32; ++width) {
            float p[16]; Vector2 dp[16]; reset(p, dp, hidden, 0);
            scanners[scan](color + 4, depth + 4, width, p, dp, &texture);
        }
        printf("SHORT_TIME hidden=%u seconds=%.6f repetitions=%u\n", hidden, seconds() - start, repeats);
    }
    return status;
}
