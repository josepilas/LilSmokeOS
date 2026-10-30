#define _POSIX_C_SOURCE 200809L
#include "m8-damage.h"
#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

static int damage_fd = -1;
static unsigned char *damage_map;
static size_t damage_span;
static uint64_t damage_start, damage_us, damage_bytes;
static unsigned damage_frames;

typedef uint32_t DamageWord __attribute__((__may_alias__));
static void damage_copy_opaque(unsigned char *destination, const unsigned char *source, unsigned count) {
    if (((uintptr_t)destination | (uintptr_t)source) % sizeof(uint32_t) == 0) {
        DamageWord *out = (DamageWord *)destination;
        const DamageWord *in = (const DamageWord *)source;
        while (count >= 4) {
            out[0] = in[0] | UINT32_C(0xff000000);
            out[1] = in[1] | UINT32_C(0xff000000);
            out[2] = in[2] | UINT32_C(0xff000000);
            out[3] = in[3] | UINT32_C(0xff000000);
            out += 4; in += 4; count -= 4;
        }
        while (count--) *out++ = *in++ | UINT32_C(0xff000000);
        return;
    }
    for (unsigned x = 0; x < count; ++x) {
        uint32_t pixel;
        memcpy(&pixel, source + (size_t)x * 4, 4);
        pixel |= UINT32_C(0xff000000);
        memcpy(destination + (size_t)x * 4, &pixel, 4);
    }
}

static uint64_t damage_now(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts)) return 0;
    return (uint64_t)ts.tv_sec * 1000000U + ts.tv_nsec / 1000U;
}
void lilsmoke_damage_reset(void) {
    if (damage_map) munmap(damage_map, damage_span);
    if (damage_fd >= 0) close(damage_fd);
    damage_map = NULL; damage_fd = -1; damage_span = 0;
    damage_start = damage_us = damage_bytes = damage_frames = 0;
}
static int damage_fail(const char *stage) {
    const int error = errno ? errno : EINVAL;
    fprintf(stderr, "M8Damage rejected stage=%s errno=%d\n", stage, error);
    lilsmoke_damage_reset(); errno = error; return 0;
}

int m8_damage_present(const void *pixels, unsigned width, unsigned height,
                           unsigned pitch, const int32_t *rects, unsigned count) {
    const char *enabled = getenv("LILSMOKE_HCFB");
    if (!enabled || strcmp(enabled, "1") || !pixels || !width || !height ||
        width > 1920 || height > 1080 || pitch < width * 4U || pitch > 16384 ||
        count > 2048 || (count && !rects)) return 0;
    if (!count) return 1;
    /* Validate the entire batch before touching mapped memory. */
    for (unsigned i = 0; i < count; ++i) {
        const int32_t *r = rects + i * 4;
        if (r[0] < 0 || r[1] < 0 || r[2] <= 0 || r[3] <= 0 ||
            (uint64_t)r[0] + r[2] > width || (uint64_t)r[1] + r[3] > height) return 0;
    }
    if (damage_fd < 0) damage_fd = open("/dev/fb0", O_RDWR | O_CLOEXEC);
    if (damage_fd < 0) return damage_fail("open");
    struct fb_var_screeninfo v;
    struct fb_fix_screeninfo f;
    if (ioctl(damage_fd, FBIOGET_VSCREENINFO, &v) || ioctl(damage_fd, FBIOGET_FSCREENINFO, &f)) return damage_fail("screen-info");
    uint64_t last = ((uint64_t)v.yoffset + height - 1U) * f.line_length +
                    ((uint64_t)v.xoffset + width) * 4U;
    if (memcmp(f.id, "hcfb", 4) || v.bits_per_pixel != 32 ||
        v.xres != width || v.yres != height || v.red.offset != 16 || v.red.length != 8 ||
        v.green.offset != 8 || v.green.length != 8 || v.blue.offset || v.blue.length != 8 ||
        (v.transp.length && (v.transp.offset != 24 || v.transp.length != 8)) ||
        !f.smem_len || f.smem_len > 32U * 1024U * 1024U || last > f.smem_len ||
        (uint64_t)v.xoffset + width > v.xres_virtual || (uint64_t)v.yoffset + height > v.yres_virtual ||
        ((uint64_t)v.xoffset + width) * 4U > f.line_length) {
        fprintf(stderr, "M8Damage geometry source=%ux%u fb=%ux%u virtual=%ux%u offset=%u,%u bpp=%u stride=%u span=%u\n",
                width, height, v.xres, v.yres, v.xres_virtual, v.yres_virtual,
                v.xoffset, v.yoffset, v.bits_per_pixel, f.line_length, f.smem_len);
        errno = EINVAL; return damage_fail("geometry");
    }
    const size_t visible_span = (size_t)last;
    if (damage_map && damage_span != visible_span) lilsmoke_damage_reset();
    if (!damage_map) {
        if (damage_fd < 0) damage_fd = open("/dev/fb0", O_RDWR | O_CLOEXEC);
        if (damage_fd < 0 || ioctl(damage_fd, 0x20000d04UL, 0UL)) return damage_fail("cache-mode");
        /* Match the visible scanout, not the vendor's unused multi-buffer allocation. */
        void *mapping = mmap(NULL, visible_span, PROT_READ | PROT_WRITE, MAP_SHARED, damage_fd, 0);
        if (mapping == MAP_FAILED) return damage_fail("mmap");
        damage_map = mapping; damage_span = visible_span;
        damage_start = damage_now();
    }
    uint64_t began = damage_now();
    for (unsigned i = 0; i < count; ++i) {
        const int32_t *r = rects + i * 4;
        for (int32_t y = r[1]; y < r[1] + r[3]; ++y) {
            const unsigned char *source = (const unsigned char *)pixels + (size_t)y * pitch + (size_t)r[0] * 4;
            unsigned char *destination = damage_map + ((size_t)y + v.yoffset) * f.line_length +
                                         ((size_t)r[0] + v.xoffset) * 4;
            damage_copy_opaque(destination, source, (unsigned)r[2]);
        }
        damage_bytes += (uint64_t)r[2] * r[3] * 4U;
    }
    damage_us += damage_now() - began; ++damage_frames;
    uint64_t now = damage_now();
    if (now - damage_start >= 5000000 && damage_frames) {
        fprintf(stderr, "M8Damage frames=%u elapsed_us=%llu bytes=%llu copy_us_avg=%llu\n", damage_frames,
                (unsigned long long)(now - damage_start), (unsigned long long)damage_bytes,
                (unsigned long long)(damage_us / damage_frames));
        damage_start = now; damage_us = damage_bytes = damage_frames = 0;
    }
    return 1;
}
