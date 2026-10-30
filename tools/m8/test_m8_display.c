#define _GNU_SOURCE
#include <assert.h>
#include <stdarg.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int opens, blanks, scales, queries;
static int mock_open(const char *path, int flags, ...) {
    (void)flags; ++opens;
    if (!strcmp(path, "/dev/fb0")) return 101;
    if (!strcmp(path, "/dev/dis")) return 102;
    assert(0); return -1;
}
static int mock_close(int fd) { assert(fd == 101 || fd == 102); return 0; }
static int mock_ioctl(int fd, unsigned long request, ...);
#define open mock_open
#define close mock_close
#define ioctl mock_ioctl
#define M8_DISPLAY_TEST
#include "../../board/segam-m8-v6-stock/m8-display.c"
#undef open
#undef close
#undef ioctl

static int mock_ioctl(int fd, unsigned long request, ...) {
    va_list args; va_start(args, request);
    if (request == FBIOGET_VSCREENINFO) {
        assert(fd == 101);
        struct fb_var_screeninfo *v = va_arg(args, void *);
        memset(v, 0, sizeof(*v));
        v->xres = v->xres_virtual = 1280; v->yres = 720; v->yres_virtual = 2880;
        v->bits_per_pixel = 32;
    } else if (request == FBIOGET_FSCREENINFO) {
        struct fb_fix_screeninfo *f = va_arg(args, void *);
        memset(f, 0, sizeof(*f)); memcpy(f->id, "hcfb", 4);
        f->line_length = 5120; f->smem_len = 14749696;
    } else if (request == HC_GET_SCREEN) {
        assert(fd == 102); ++queries;
        struct hc_screen *s = va_arg(args, void *);
        assert(s->type == 1); s->width = 1920; s->height = 1080;
    } else if (request == HC_SET_SCALE) {
        assert(fd == 101); ++scales;
        struct hc_scale *s = va_arg(args, void *);
        assert(s->h_div == 1280 && s->v_div == 720 && s->h_mul == 1920 && s->v_mul == 1080);
    } else if (request == FBIOBLANK) {
        int value = va_arg(args, int);
        assert(value == (blanks++ % 2 ? FB_BLANK_UNBLANK : FB_BLANK_NORMAL));
    } else assert(0);
    va_end(args); return 0;
}

int main(void) {
    unsetenv("LILSMOKE_HCFB"); after_present(); assert(opens == 0);
    frames = 0; setenv("LILSMOKE_HCFB", "1", 1);
    after_present(); after_present(); after_present();
    assert(queries == 2 && scales == 2 && blanks == 4);
    assert(component(0xf800, (struct fb_bitfield){11, 5, 0}) == 255);
    assert(component(0x07e0, (struct fb_bitfield){5, 6, 0}) == 255);
    assert(component(0x001f, (struct fb_bitfield){0, 5, 0}) == 255);
    struct fb_var_screeninfo v = {.xres=1280, .yres=720, .bits_per_pixel=32};
    struct fb_fix_screeninfo f = {.line_length=5120, .smem_len=14749696};
    assert(valid_geometry(&v, &f));
    v.yoffset = 2800; assert(!valid_geometry(&v, &f));
    v.yoffset = 0; f.line_length = 4096; assert(!valid_geometry(&v, &f));
    puts("PASS: gated HC16xx ioctls, exact HDMI scaling, unblank order, RGB565, framebuffer bounds. No hardware accessed.");
    return 0;
}
