#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
#include <SDL.h>

static struct fb_var_screeninfo hardware;
static unsigned char memory[2 * 1024 * 1024];
static int opened, mode_puts, pans, bad_id, reject_mode, bad_memory, fail_map, fail_pan;
static unsigned delay_us;
static uint64_t time_us;
static size_t mapped;
static int fake_open(const char *path, int flags, ...) {
    (void)flags; ++opened;
    if (!strcmp(path, "/dev/fb0")) return 101;
    assert(!strcmp(path, "/dev/dis")); return 102;
}
static int fake_close(int fd) { assert(fd == 101 || fd == 102); return 0; }
static int fake_ioctl(int fd, unsigned long req, ...);
static void *fake_mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset) {
    assert(!addr && (prot & PROT_WRITE) && flags == MAP_SHARED && fd == 101 && !offset);
    assert(length < sizeof(memory)); mapped = length;
    if (fail_map) { errno = ENOMEM; return MAP_FAILED; }
    memset(memory, 0xcc, sizeof(memory)); return memory;
}
static int fake_munmap(void *addr, size_t length) { assert(addr == memory && length == mapped); return 0; }
static int fake_clock(clockid_t clock, struct timespec *ts) {
    assert(clock == CLOCK_MONOTONIC);
    ts->tv_sec = time_us / 1000000; ts->tv_nsec = (time_us % 1000000) * 1000;
    time_us += 100; return 0;
}
#define open fake_open
#define close fake_close
#define ioctl fake_ioctl
#define mmap fake_mmap
#define munmap fake_munmap
#define clock_gettime fake_clock
#include "../../board/segam-m8-v6-stock/m8-native-video.c"
#undef open
#undef close
#undef ioctl
#undef mmap
#undef munmap
#undef clock_gettime

static int fake_ioctl(int fd, unsigned long req, ...) {
    va_list ap; va_start(ap, req);
    int result = 0;
    if (req == FBIOGET_FSCREENINFO) {
        assert(fd == 101);
        struct fb_fix_screeninfo *f = va_arg(ap, void *);
        memset(f, 0, sizeof(*f)); memcpy(f->id, bad_id ? "cirrus" : "hcfb", bad_id ? 6 : 4);
        f->line_length = (hardware.xres * (hardware.bits_per_pixel / 8) + 31) & ~31;
        f->smem_len = bad_memory ? 1 : sizeof(memory);
    } else if (req == FBIOGET_VSCREENINFO) {
        *va_arg(ap, struct fb_var_screeninfo *) = hardware;
    } else if (req == NATIVE_GET_SCREEN) {
        assert(fd == 102);
        struct native_screen *s = va_arg(ap, void *);
        assert(s->type == 1); s->width = 1920; s->height = 1080;
    } else if (req == FBIOPUT_VSCREENINFO) {
        struct fb_var_screeninfo *v = va_arg(ap, void *); ++mode_puts;
        if (reject_mode && v->bits_per_pixel == 16) { errno = EINVAL; result = -1; }
        else hardware = *v;
    } else if (req == NATIVE_SET_SCALE) {
        struct native_scale *s = va_arg(ap, void *);
        assert(s->h_div == hardware.xres && s->v_div == hardware.yres);
        assert(s->h_mul == 1920 && s->v_mul == 1080);
    } else if (req == NATIVE_MMAP_CACHE) {
        assert(va_arg(ap, unsigned long) == 0);
    } else if (req == FBIOPAN_DISPLAY) {
        struct fb_var_screeninfo *v = va_arg(ap, void *); ++pans;
        if (video.active && fail_pan) { errno = EIO; result = -1; }
        else { hardware.yoffset = v->yoffset; if (video.active) time_us += delay_us; }
    } else {
        assert(req == FBIOBLANK);
        int value = va_arg(ap, int); assert(value == 0 || value == 1);
    }
    va_end(ap); return result;
}

static void initial(void) {
    m8_native_video_reset();
    hardware = (struct fb_var_screeninfo){ .xres=856, .xres_virtual=856,
        .yres=480, .yres_virtual=480, .bits_per_pixel=32,
        .red={16,8,0}, .green={8,8,0}, .blue={0,8,0}, .transp={24,8,0} };
    opened = mode_puts = pans = bad_id = reject_mode = bad_memory = fail_map = fail_pan = 0;
    delay_us = 0;
}

int main(void) {
    SDL_PixelFormat format = { .BitsPerPixel=16, .BytesPerPixel=2,
        .Rmask=0xf800, .Gmask=0x07e0, .Bmask=0x001f };
    unsigned char pixels[640 * 240];
    for (unsigned i = 0; i < sizeof(pixels); ++i) pixels[i] = (unsigned char)(i * 13);
    SDL_Surface surface = { .w=256, .h=224, .pitch=544, .pixels=pixels, .format=&format };
    initial(); unsetenv("LILSMOKE_M8_NATIVE_VIDEO");
    assert(!m8_native_present(&surface) && !opened);
    setenv("LILSMOKE_M8_NATIVE_VIDEO", "1", 1);
    format.Rmask=0; assert(!m8_native_present(&surface) && !opened); format.Rmask=0xf800;
    assert(m8_native_present(&surface) && hardware.xres == 344 && hardware.bits_per_pixel == 16);
    assert(video.pages == 2 && video.front == 1);
    for (int y = 0; y < surface.h; ++y) {
        unsigned char *row = memory + (size_t)(surface.h + y) * video.fixed.line_length;
        assert(!memcmp(row + video.left * 2, pixels + y * surface.pitch, surface.w * 2));
        for (unsigned x = 0; x < video.left * 2; ++x) assert(!row[x]);
    }
    assert(memory[mapped] == 0xcc);
    assert(m8_native_present(&surface) && video.front == 0);
    delay_us = 10000;
    assert(m8_native_present(&surface) && video.pages == 1 && video.front == 1);
    int before = pans;
    assert(m8_native_present(&surface) && pans == before);
    m8_native_video_reset(); assert(hardware.xres == 856 && hardware.bits_per_pixel == 32);

    initial(); surface.w=320; surface.h=240; surface.pitch=640;
    assert(m8_native_present(&surface) && video.current.xres == 424);
    initial(); bad_id=1; assert(!m8_native_present(&surface) && !mode_puts);
    initial(); reject_mode=1; assert(!m8_native_present(&surface) && hardware.bits_per_pixel == 32);
    initial(); bad_memory=1; assert(!m8_native_present(&surface) && hardware.bits_per_pixel == 32);
    initial(); fail_map=1; assert(!m8_native_present(&surface) && hardware.bits_per_pixel == 32);
    initial(); assert(m8_native_present(&surface)); fail_pan=1;
    assert(!m8_native_present(&surface) && hardware.bits_per_pixel == 32);
    m8_native_video_reset();
    initial();
    uint32_t gba[240 * 160];
    for (unsigned i = 0; i < 240 * 160; ++i) gba[i] = 0x00ff0000;
    assert(lilsmoke_present_rgb(gba, 240, 160, 960, 1, 3, 2));
    assert(video.width == 240 && video.height == 160);
    uint16_t red;
    memcpy(&red, video.map + (size_t)video.front * video.height * video.fixed.line_length + video.left * 2, 2);
    assert(red == 0xf800);
    assert(!lilsmoke_present_rgb(gba, 240, 160, 70000, 1, 3, 2));
    uint16_t menu[426 * 240];
    for (unsigned y = 0; y < 240; ++y)
        for (unsigned x = 0; x < 426; ++x)
            menu[y * 426 + x] = y < 120 ? (x < 213 ? 0xf00f : 0x0f0f) : (x < 213 ? 0x00ff : 0xffff);
    assert(lilsmoke_present_menu(menu, 426, 240, 852, 0));
    assert(video.width == 426 && video.height == 240 && video.current.xres == 432);
    assert(video.before.xres == 856 && video.before.bits_per_pixel == 32);
    unsigned char *page = video.map + (size_t)video.front * 240 * video.fixed.line_length + video.left * 2;
    uint16_t corner;
    memcpy(&corner, page, 2); assert(corner == 0xf800);
    memcpy(&corner, page + 425 * 2, 2); assert(corner == 0x07e0);
    memcpy(&corner, page + 239 * video.fixed.line_length, 2); assert(corner == 0x001f);
    memcpy(&corner, page + 239 * video.fixed.line_length + 425 * 2, 2); assert(corner == 0xffff);
    hardware.xres = hardware.xres_virtual = 856; hardware.yres = hardware.yres_virtual = 480;
    hardware.bits_per_pixel = 32; hardware.red.offset = 16; hardware.green.offset = 8;
    assert(lilsmoke_present_menu(menu, 426, 240, 852, 0));
    assert(hardware.xres == 432 && hardware.yres == 240 && hardware.bits_per_pixel == 16);
    assert(lilsmoke_present_rgb(gba, 240, 160, 960, 1, 3, 2));
    assert(video.current.xres == 288 && video.height == 160 && video.before.xres == 856);
    assert(lilsmoke_present_menu(gba, 240, 160, 960, 1));
    assert(video.width == 240 && video.height == 160);
    assert(!lilsmoke_present_menu(menu, 641, 240, 1282, 0));
    initial();
    uint32_t *wide = calloc(640 * 480, 4); assert(wide);
    assert(lilsmoke_present_rgb(wide,640,480,2560,1,4,3));
    assert(video.current.xres == 856 && video.height == 480);
    free(wide);
    initial();
    SDL_Color colors[2] = {{255,0,0,0},{0,255,0,0}};
    SDL_Palette pal = {.ncolors=2,.colors=colors};
    SDL_PixelFormat indexed = {.BitsPerPixel=8,.BytesPerPixel=1,.palette=&pal};
    unsigned char indices[4] = {0,1,1,0};
    SDL_Surface small = {.w=2,.h=2,.pitch=2,.format=&indexed,.pixels=indices};
    setenv("LILSMOKE_OD_NATIVE","1",1);
    assert(m8_od_present(&small));
    uint16_t *rgb = (uint16_t *)(video.map + video.front * video.height * video.fixed.line_length + video.left*2);
    assert(rgb[0]==0xf800 && rgb[1]==0x07e0);
    unsetenv("LILSMOKE_OD_NATIVE");assert(!m8_od_present(&small));
    m8_native_video_reset();
    assert(hardware.xres == 856 && hardware.bits_per_pixel == 32);
    puts("PASS: native RGB565 rows/pitch/pillars, framebuffer bounds, double/front buffer, slow-pan escape, reset/restore and fail-closed fallback. Mock devices only.");
    return 0;
}
