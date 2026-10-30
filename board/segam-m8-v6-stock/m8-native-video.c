#define _GNU_SOURCE
#include "m8-native-video.h"
#include <SDL.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#define NATIVE_GET_SCREEN 0xc00c0e0cUL
#define NATIVE_SET_SCALE 0x80080d00UL
/* Stock driver.so 0x51c4..0x51dc issues this request with integer 1.
 * The matching SDK enum defines 0 as uncached and 1 as cached. */
#define NATIVE_MMAP_CACHE 0x20000d04UL
struct native_screen { int32_t type; uint16_t x, y, width, height; };
struct native_scale { uint16_t h_div, v_div, h_mul, v_mul; };
_Static_assert(sizeof(struct native_screen) == 12, "screen ABI");
_Static_assert(sizeof(struct native_scale) == 8, "scaler ABI");
static struct {
    int fd, active, disabled, saved, pages, front;
    int width, height;
    unsigned aspect_num, aspect_den;
    unsigned left;
    size_t span;
    unsigned char *map;
    struct fb_var_screeninfo before, current;
    struct fb_fix_screeninfo fixed;
    struct native_screen screen;
    uint64_t start, copy_us, pan_us;
    unsigned count;
} video = { .fd = -1 };

static uint64_t native_now(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts)) return 0;
    return (uint64_t)ts.tv_sec * 1000000U + ts.tv_nsec / 1000U;
}

static int native_rgb565(const SDL_Surface *s) {
    return s && s->pixels && s->format && s->format->BitsPerPixel == 16 &&
        s->format->BytesPerPixel == 2 && s->format->Rmask == 0xf800 &&
        s->format->Gmask == 0x07e0 && s->format->Bmask == 0x001f &&
        s->w > 0 && s->h > 0 && s->w <= 640 && s->h <= 480 && s->pitch >= s->w * 2;
}

static void native_scale_to_screen(unsigned w, unsigned h) {
    struct native_scale scale = { (uint16_t)w, (uint16_t)h,
                                  video.screen.width, video.screen.height };
    (void)ioctl(video.fd, NATIVE_SET_SCALE, &scale);
}

void m8_native_video_reset(void) {
    if (video.map) (void)munmap(video.map, video.span);
    video.map = NULL;
    if (video.fd >= 0) {
        if (video.saved) {
            if (ioctl(video.fd, FBIOPUT_VSCREENINFO, &video.before))
                fprintf(stderr, "M8NativeVideo restore_failed errno=%d\n", errno);
            else {
                (void)ioctl(video.fd, FBIOPAN_DISPLAY, &video.before);
                native_scale_to_screen(video.before.xres, video.before.yres);
                (void)ioctl(video.fd, FBIOBLANK, FB_BLANK_UNBLANK);
            }
        }
        (void)close(video.fd);
    }
    memset(&video, 0, sizeof(video));
    video.fd = -1;
}

static int native_fail(const char *stage) {
    int error = errno;
    m8_native_video_reset();
    video.disabled = 1;
    fprintf(stderr, "M8NativeVideo fallback stage=%s errno=%d\n", stage, error);
    return 0;
}

static int native_configure(SDL_Surface *s, unsigned aspect_num, unsigned aspect_den) {
    video.fd = open("/dev/fb0", O_RDWR | O_CLOEXEC);
    if (video.fd < 0) return native_fail("open");
    if (ioctl(video.fd, FBIOGET_FSCREENINFO, &video.fixed) ||
        ioctl(video.fd, FBIOGET_VSCREENINFO, &video.before)) return native_fail("query");
    if (memcmp(video.fixed.id, "hcfb", 4)) { errno = ENODEV; return native_fail("not-hcfb"); }
    if (!video.before.xres || !video.before.yres || video.before.xres > 1920 ||
        video.before.yres > 1080 || !video.fixed.smem_len ||
        video.fixed.smem_len > 32U * 1024U * 1024U) {
        errno = EPROTO; return native_fail("original-bounds");
    }
    int display = open("/dev/dis", O_RDWR | O_CLOEXEC);
    if (display < 0) return native_fail("display-open");
    video.screen = (struct native_screen){ .type = 1 };
    int result = ioctl(display, NATIVE_GET_SCREEN, &video.screen);
    int error = errno;
    (void)close(display);
    if (result || !video.screen.width || !video.screen.height ||
        video.screen.width > 4096 || video.screen.height > 2160) {
        errno = result ? error : EPROTO;
        return native_fail("display-query");
    }
    /* Native SNES pixels are not square on a 4:3 TV. Pad horizontally so
     * the known full-screen scaler supplies that aspect without CPU scaling. */
    unsigned canvas = ((uint64_t)s->w * video.screen.width * aspect_den +
                        (uint64_t)video.screen.height * aspect_num / 2U) /
                        ((uint64_t)video.screen.height * aspect_num);
    canvas = (canvas + 4U) & ~7U;
    /* RGUI's 426-pixel widescreen frame must not round down to 424. */
    if (canvas < (unsigned)s->w) canvas = ((unsigned)s->w + 7U) & ~7U;
    if (canvas < (unsigned)s->w || canvas > 1024) { errno = ENOTSUP; return native_fail("aspect"); }
    video.saved = 1;
    video.current = video.before;
    video.current.xres = video.current.xres_virtual = canvas;
    video.current.yres = s->h;
    video.current.yres_virtual = s->h * 2;
    video.current.xoffset = video.current.yoffset = 0;
    video.current.bits_per_pixel = 16;
    video.current.grayscale = video.current.nonstd = video.current.rotate = 0;
    video.current.activate = FB_ACTIVATE_NOW;
    video.current.red = (struct fb_bitfield){11, 5, 0};
    video.current.green = (struct fb_bitfield){5, 6, 0};
    video.current.blue = (struct fb_bitfield){0, 5, 0};
    video.current.transp = (struct fb_bitfield){0, 0, 0};
    if (ioctl(video.fd, FBIOPUT_VSCREENINFO, &video.current)) {
        video.current.yres_virtual = s->h;
        if (ioctl(video.fd, FBIOPUT_VSCREENINFO, &video.current)) return native_fail("mode");
    }
    if (ioctl(video.fd, FBIOGET_VSCREENINFO, &video.current) ||
        ioctl(video.fd, FBIOGET_FSCREENINFO, &video.fixed)) return native_fail("mode-readback");
    struct fb_var_screeninfo *v = &video.current;
    if (v->xres < (unsigned)s->w || v->xres > 1024 || v->yres != (unsigned)s->h ||
        v->xres_virtual < v->xres || v->yres_virtual < v->yres ||
        v->xoffset || v->yoffset || memcmp(video.fixed.id, "hcfb", 4) ||
        v->bits_per_pixel != 16 || v->red.offset != 11 || v->red.length != 5 ||
        v->green.offset != 5 || v->green.length != 6 || v->blue.offset || v->blue.length != 5 ||
        video.fixed.line_length < v->xres * 2U || !video.fixed.smem_len ||
        video.fixed.smem_len > 32U * 1024U * 1024U) {
        errno = EPROTO; return native_fail("format");
    }
    video.pages = v->yres_virtual >= v->yres * 2U ? 2 : 1;
    uint64_t span = (uint64_t)video.fixed.line_length * v->yres * video.pages;
    if (!span || span > video.fixed.smem_len || span > 4U * 1024U * 1024U) {
        errno = EOVERFLOW; return native_fail("bounds");
    }
    video.span = (size_t)span;
    /* An uncached mapping avoids per-frame MIPS cacheflush syscalls and
     * exposes only the small native pixel buffer to the display controller. */
    if (ioctl(video.fd, NATIVE_MMAP_CACHE, 0UL)) return native_fail("uncached-map");
    void *map = mmap(NULL, video.span, PROT_READ | PROT_WRITE, MAP_SHARED, video.fd, 0);
    if (map == MAP_FAILED) return native_fail("mmap");
    video.map = map;
    memset(video.map, 0, video.span);
    video.left = ((v->xres - s->w) / 2U) & ~1U;
    video.width = s->w; video.height = s->h;
    video.aspect_num = aspect_num; video.aspect_den = aspect_den;
    struct native_scale scale = { v->xres, v->yres, video.screen.width, video.screen.height };
    if (ioctl(video.fd, NATIVE_SET_SCALE, &scale)) return native_fail("scale");
    v->xoffset = v->yoffset = 0;
    if (ioctl(video.fd, FBIOPAN_DISPLAY, v)) return native_fail("initial-pan");
    (void)ioctl(video.fd, FBIOBLANK, FB_BLANK_NORMAL);
    if (ioctl(video.fd, FBIOBLANK, FB_BLANK_UNBLANK)) return native_fail("unblank");
    video.active = 1; video.start = native_now();
    fprintf(stderr, "M8NativeVideo active source=%dx%d canvas=%ux%u bpp=16 stride=%u pages=%d screen=%ux%u cpu_scaling=0\n",
            s->w, s->h, v->xres, v->yres, video.fixed.line_length,
            video.pages, video.screen.width, video.screen.height);
    return 1;
}

static int native_present_aspect(void *opaque, unsigned aspect_num, unsigned aspect_den) {
    SDL_Surface *s = opaque;
    const char *setting = getenv("LILSMOKE_M8_NATIVE_VIDEO");
    if (!setting || strcmp(setting, "1") || video.disabled || !native_rgb565(s) ||
        !aspect_num || !aspect_den || aspect_num > 100000 || aspect_den > 100000) return 0;
    static int loading_lookup;
    static int (*loading_prepare)(const void *, unsigned, unsigned, unsigned);
    if (!loading_lookup) {
        loading_prepare = dlsym(RTLD_DEFAULT, "lilsmoke_loading_prepare_rgb");
        loading_lookup = 1;
    }
    if (loading_prepare) {
        if (loading_prepare(s->pixels, s->w, s->h, s->pitch)) return 1;
        loading_prepare = NULL;
    }
    if (video.active && (s->w != video.width || s->h != video.height ||
        video.aspect_num != aspect_num || video.aspect_den != aspect_den)) m8_native_video_reset();
    if (!video.active && !native_configure(s, aspect_num, aspect_den)) return 0;
    int page = video.pages == 2 ? 1 - video.front : video.front;
    uint64_t start = native_now();
    unsigned char *destination = video.map + (size_t)page * video.height * video.fixed.line_length + video.left * 2U;
    const unsigned char *source = s->pixels;
    for (int y = 0; y < s->h; ++y) {
        memcpy(destination, source, (size_t)s->w * 2U);
        destination += video.fixed.line_length;
        source += s->pitch;
    }
    video.copy_us += native_now() - start;
    if (video.pages == 2) {
        video.current.yoffset = page * video.height;
        start = native_now();
        if (ioctl(video.fd, FBIOPAN_DISPLAY, &video.current)) return native_fail("pan");
        uint64_t elapsed = native_now() - start;
        video.pan_us += elapsed;
        video.front = page;
        if (elapsed > 8000) {
            video.pages = 1;
            fprintf(stderr, "M8NativeVideo pan_wait_us=%llu switching_to_front_buffer=1\n", (unsigned long long)elapsed);
        }
    }
    ++video.count;
    uint64_t now = native_now();
    if (now - video.start >= 5000000 && video.count) {
        fprintf(stderr, "M8NativeVideo perf frames=%u source=%dx%d elapsed_us=%llu copy_us=%llu pan_us=%llu\n", video.count,
                video.width, video.height, (unsigned long long)(now - video.start),
                (unsigned long long)(video.copy_us / video.count),
                (unsigned long long)(video.pan_us / video.count));
        video.start = now; video.count = 0; video.copy_us = video.pan_us = 0;
    }
    return 1;
}

int m8_native_present(void *opaque) { return native_present_aspect(opaque, 4, 3); }
void lilsmoke_present_reset(void) { m8_native_video_reset(); }

static uint16_t *rgb_buffer;
static size_t rgb_capacity;
static int reserve_rgb(unsigned width, unsigned height) {
    size_t needed = (size_t)width * height * sizeof(uint16_t);
    if (rgb_capacity < needed) {
        void *memory = realloc(rgb_buffer, needed);
        if (!memory) return 0;
        rgb_buffer = memory; rgb_capacity = needed;
    }
    return 1;
}

int lilsmoke_present_rgb(const void *pixels, unsigned width, unsigned height,
                        unsigned pitch, int rgb32, unsigned aspect_num, unsigned aspect_den) {
    if (!pixels || !width || !height || width > 640 || height > 480 ||
        pitch > 65535 || pitch < width * (rgb32 ? 4U : 2U)) return 0;
    SDL_PixelFormat format = { .BitsPerPixel=16, .BytesPerPixel=2,
        .Rmask=0xf800, .Gmask=0x07e0, .Bmask=0x001f };
    SDL_Surface surface = { .w=(int)width, .h=(int)height, .format=&format,
                           .pitch=(Uint16)pitch, .pixels=(void *)pixels };
    if (rgb32) {
        if (!reserve_rgb(width, height)) return 0;
        for (unsigned y = 0; y < height; ++y)
            for (unsigned x = 0; x < width; ++x) {
                uint32_t value;
                memcpy(&value, (const unsigned char *)pixels + (size_t)y * pitch + x * 4U, 4);
                rgb_buffer[(size_t)y * width + x] = ((value >> 8) & 0xf800) |
                    ((value >> 5) & 0x07e0) | ((value >> 3) & 0x001f);
            }
        surface.pixels = rgb_buffer; surface.pitch = width * 2U;
    }
    return native_present_aspect(&surface, aspect_num, aspect_den);
}

int m8_od_present(void *opaque) {
    SDL_Surface *s = opaque;
    const char *setting = getenv("LILSMOKE_OD_NATIVE");
    if (!setting || strcmp(setting, "1") || !s || !s->pixels || !s->format ||
        s->w < 1 || s->h < 1 || s->w > 640 || s->h > 480 ||
        !s->format->BytesPerPixel || s->format->BytesPerPixel > 4 ||
        s->pitch < s->w * s->format->BytesPerPixel) return 0;
    unsigned num = s->w, den = s->h;
    const char *engine = getenv("LILSMOKE_OD_ENGINE");
    if (engine && (!strcmp(engine,"picodrive") || !strcmp(engine,"fceu320") ||
        !strcmp(engine,"temper") || !strcmp(engine,"mame4all") || !strcmp(engine,"atari800"))) {
        num = 4; den = 3;
    }
    if (native_rgb565(s)) return native_present_aspect(s, num, den);
    if (!reserve_rgb(s->w, s->h)) return 0;
    SDL_PixelFormat *f = s->format;
    uint16_t palette[256];
    if (f->BytesPerPixel == 1) {
        if (!f->palette || !f->palette->colors || f->palette->ncolors < 1 || f->palette->ncolors > 256) return 0;
        memset(palette, 0, sizeof(palette));
        for (int i = 0; i < f->palette->ncolors; ++i) {
            SDL_Color c = f->palette->colors[i];
            palette[i] = ((c.r >> 3) << 11) | ((c.g >> 2) << 5) | (c.b >> 3);
        }
    } else if (f->Rshift > 31 || f->Gshift > 31 || f->Bshift > 31 ||
               f->Rloss > 8 || f->Gloss > 8 || f->Bloss > 8) return 0;
    for (int y = 0; y < s->h; ++y) {
        const unsigned char *row = (const unsigned char *)s->pixels + (size_t)y * s->pitch;
        for (int x = 0; x < s->w; ++x) {
            uint16_t color;
            if (f->BytesPerPixel == 1) color = palette[row[x]];
            else {
                uint32_t value = 0;
                memcpy(&value, row + x * f->BytesPerPixel, f->BytesPerPixel);
                unsigned r = ((value & f->Rmask) >> f->Rshift) << f->Rloss;
                unsigned g = ((value & f->Gmask) >> f->Gshift) << f->Gloss;
                unsigned b = ((value & f->Bmask) >> f->Bshift) << f->Bloss;
                color = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
            }
            rgb_buffer[(size_t)y * s->w + x] = color;
        }
    }
    return lilsmoke_present_rgb(rgb_buffer, s->w, s->h, s->w * 2U, 0, num, den);
}

int lilsmoke_present_menu(const void *pixels, unsigned width, unsigned height,
                         unsigned pitch, int rgb32) {
    if (video.disabled || !pixels || !width || !height || width > 640 || height > 480 ||
        pitch > 65535 || pitch < width * (rgb32 ? 4U : 2U)) return 0;
    if (video.active) {
        struct fb_var_screeninfo current;
        if (ioctl(video.fd, FBIOGET_VSCREENINFO, &current)) return native_fail("menu-query");
        if (current.xres != video.current.xres || current.yres != video.current.yres ||
            current.bits_per_pixel != video.current.bits_per_pixel ||
            current.red.offset != 11 || current.green.offset != 5 || current.blue.offset != 0)
            m8_native_video_reset();
    }
    if (rgb32) return lilsmoke_present_rgb(pixels, width, height, pitch, 1, width, height);
    if (!reserve_rgb(width, height)) return 0;
    /* SDL2's RGUI supplies RGBA4444, not the game's RGB565. Present an opaque menu
     * at its own small resolution; no 1280x720 texture scaling or blending. */
    for (unsigned y = 0; y < height; ++y)
        for (unsigned x = 0; x < width; ++x) {
            uint16_t value;
            memcpy(&value, (const unsigned char *)pixels + (size_t)y * pitch + x * 2U, 2);
            unsigned r = (value >> 12) & 15U, g = (value >> 8) & 15U, b = (value >> 4) & 15U;
            rgb_buffer[(size_t)y * width + x] = (((r << 1) | (r >> 3)) << 11) |
                (((g << 2) | (g >> 2)) << 5) | ((b << 1) | (b >> 3));
        }
    SDL_PixelFormat format = { .BitsPerPixel=16, .BytesPerPixel=2,
        .Rmask=0xf800, .Gmask=0x07e0, .Bmask=0x001f };
    SDL_Surface surface = { .w=(int)width, .h=(int)height, .format=&format,
                           .pitch=(Uint16)(width * 2U), .pixels=rgb_buffer };
    return native_present_aspect(&surface, width, height);
}
