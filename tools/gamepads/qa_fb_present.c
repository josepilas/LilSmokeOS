/* SPDX-License-Identifier: MPL-2.0 */
/* Private QA only: substitute cirrus fbdev scanout for the absent HC16xx scaler. */
#define _GNU_SOURCE
#include <fcntl.h>
#include <linux/fb.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

static struct fb_fix_screeninfo fix;
static struct fb_var_screeninfo var;
static unsigned char *scanout;
static int fb_fd = -1;
static unsigned mapped_bytes;
static unsigned frames, menus;
static unsigned channel(unsigned value, struct fb_bitfield field) {
    if (!field.length) return 0;
    return ((value * ((1U << field.length) - 1U) + 127U) / 255U) << field.offset;
}
static int display(const void *pixels, unsigned w, unsigned h, unsigned pitch,
                   int rgb32, unsigned num, unsigned den, int menu) {
    if (!pixels || !w || !h || w > 2048 || h > 2048 ||
        pitch < w * (rgb32 ? 4U : 2U) || pitch > 16384 || !num || !den)
        return 0;
    if (menu) ++menus; else ++frames;
    if (frames + menus == 1) {
        fprintf(stderr, "QA_PRESENT_FRAME width=%u height=%u rgb32=%d physical_hc16xx=0\n", w, h, rgb32);
    }
    if (!getenv("LILSMOKE_QA_FB_PRESENT")) return 1;
    /* SDL can change Cirrus mode between game and RGUI. Never keep an old
     * stride/pixel format across that transition, unlike the fixed TV scaler. */
    if (fb_fd < 0) fb_fd = open("/dev/fb0", O_RDWR | O_CLOEXEC);
    if (fb_fd < 0) return 0;
    {
        int invalid = ioctl(fb_fd, FBIOGET_FSCREENINFO, &fix) || ioctl(fb_fd, FBIOGET_VSCREENINFO, &var);
        if (invalid || var.xres > 1920 || var.yres > 1080 ||
            !var.xres || !var.yres || !fix.smem_len || fix.smem_len > 32U*1024U*1024U ||
            (var.bits_per_pixel != 16 && var.bits_per_pixel != 24 && var.bits_per_pixel != 32) ||
            var.red.length > 8 || var.green.length > 8 || var.blue.length > 8 ||
            var.red.offset + var.red.length > 32 || var.green.offset + var.green.length > 32 ||
            var.blue.offset + var.blue.length > 32) return 0;
        if (mapped_bytes != fix.smem_len) {
            if (scanout) munmap(scanout, mapped_bytes);
            scanout = mmap(NULL, fix.smem_len, PROT_READ | PROT_WRITE, MAP_SHARED, fb_fd, 0);
            if (scanout == MAP_FAILED) { scanout = NULL; mapped_bytes = 0; return 0; }
            mapped_bytes = fix.smem_len;
        }
    }
    unsigned bpp = var.bits_per_pixel / 8;
    uint64_t end = (uint64_t)(var.yoffset + var.yres - 1) * fix.line_length +
        (uint64_t)(var.xoffset + var.xres) * bpp;
    if (end > fix.smem_len) return 0;
    unsigned dw = var.xres, dh = (unsigned)((uint64_t)dw * den / num);
    if (dh > var.yres) { dh = var.yres; dw = (unsigned)((uint64_t)dh * num / den); }
    if (!dw || !dh) return 0;
    unsigned x0 = (var.xres - dw) / 2, y0 = (var.yres - dh) / 2;
    for (unsigned y = 0; y < var.yres; ++y) {
        unsigned char *dest = scanout + (var.yoffset + y) * fix.line_length + var.xoffset * bpp;
        for (unsigned x = 0; x < var.xres; ++x) {
            unsigned color = 0;
            if (x >= x0 && x < x0 + dw && y >= y0 && y < y0 + dh) {
                unsigned sx = (unsigned)((uint64_t)(x - x0) * w / dw);
                unsigned sy = (unsigned)((uint64_t)(y - y0) * h / dh);
                const unsigned char *source = (const unsigned char *)pixels + sy * pitch;
                unsigned r, g, b;
                if (rgb32) {
                    uint32_t value; memcpy(&value, source + sx * 4, 4);
                    r = (value >> 16) & 255; g = (value >> 8) & 255; b = value & 255;
                } else {
                    uint16_t value; memcpy(&value, source + sx * 2, 2);
                    if (menu) {
                        r = ((value >> 12) & 15) * 17;
                        g = ((value >> 8) & 15) * 17;
                        b = ((value >> 4) & 15) * 17;
                    } else {
                        r = ((value >> 11) & 31) * 255 / 31;
                        g = ((value >> 5) & 63) * 255 / 63;
                        b = (value & 31) * 255 / 31;
                    }
                }
                color = channel(r, var.red) | channel(g, var.green) | channel(b, var.blue);
            }
            memcpy(dest + x * bpp, &color, bpp);
        }
    }
    __sync_synchronize();
    return 1;
}
int lilsmoke_present_rgb(const void *p, unsigned w, unsigned h, unsigned pitch,
                        int rgb32, unsigned n, unsigned d) {
    return display(p, w, h, pitch, rgb32, n, d, 0);
}
int lilsmoke_present_menu(const void *p, unsigned w, unsigned h, unsigned pitch, int rgb32) {
    return display(p, w, h, pitch, rgb32, 16, 9, 1);
}
void lilsmoke_present_reset(void) {}
void lilsmoke_present_checkpoint(void) {}
__attribute__((destructor)) static void finish(void) {
    fprintf(stderr, "QA_PRESENT_DONE game_frames=%u menu_frames=%u\n", frames, menus);
    if (scanout) munmap(scanout, mapped_bytes);
    if (fb_fd >= 0) close(fb_fd);
}

