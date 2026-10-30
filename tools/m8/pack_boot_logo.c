#include <webp/decode.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static void little32(FILE *f, uint32_t n) {
    for (unsigned i = 0; i < 4; ++i) fputc((int)((n >> (i * 8U)) & 255U), f);
}

int main(int argc, char **argv) {
    if (argc != 3) return 2;
    FILE *in = fopen(argv[1], "rb");
    if (!in || fseek(in, 0, SEEK_END)) return 1;
    long size = ftell(in);
    if (size <= 0 || size > 16 * 1024 * 1024 || fseek(in, 0, SEEK_SET)) return 1;
    unsigned char *data = malloc((size_t)size);
    if (!data || fread(data, 1, (size_t)size, in) != (size_t)size) return 1;
    fclose(in);
    WebPDecoderConfig c;
    if (!WebPInitDecoderConfig(&c) || WebPGetFeatures(data, (size_t)size, &c.input) != VP8_STATUS_OK ||
        c.input.has_animation || c.input.width > 8192 || c.input.height > 8192) return 1;
    unsigned w = (unsigned)c.input.width, h = (unsigned)c.input.height;
    if (w > 1280 || h > 720) {
        if ((uint64_t)w * 720U > (uint64_t)h * 1280U) { h = (uint64_t)h * 1280U / w; w = 1280; }
        else { w = (uint64_t)w * 720U / h; h = 720; }
    }
    if (!w || !h) return 1;
    c.options.use_scaling = 1;
    c.options.scaled_width = (int)w; c.options.scaled_height = (int)h;
    c.options.use_threads = 0;
    c.output.colorspace = MODE_BGRA;
    if (WebPDecode(data, (size_t)size, &c) != VP8_STATUS_OK) return 1;
    FILE *out = fopen(argv[2], "wb");
    if (!out) return 1;
    fwrite("M8ARGB1\0", 1, 8, out); little32(out, w); little32(out, h);
    for (unsigned y = 0; y < h; ++y) {
        unsigned char *row = c.output.u.RGBA.rgba + (size_t)y * c.output.u.RGBA.stride;
        /* Deterministic black compositing, preserving the supplied artwork. */
        for (unsigned x = 0; x < w; ++x) {
            for (unsigned k = 0; k < 3; ++k) row[x * 4U + k] = (row[x * 4U + k] * row[x * 4U + 3] + 127U) / 255U;
            row[x * 4U + 3] = 255;
        }
        if (fwrite(row, 4, w, out) != w) return 1;
    }
    int failed = ferror(out);
    if (fclose(out)) failed = 1;
    fprintf(stderr, "Native WebP %d.%d.%d: %dx%d -> %ux%u BGRA, single-threaded decode/resize only\n",
            WebPGetDecoderVersion() >> 16, (WebPGetDecoderVersion() >> 8) & 255, WebPGetDecoderVersion() & 255,
            c.input.width, c.input.height, w, h);
    WebPFreeDecBuffer(&c.output); free(data);
    return failed;
}
