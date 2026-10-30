#define _GNU_SOURCE
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
#include <unistd.h>
#ifndef M8_DISPLAY_TEST
#include "m8-native-video.h"
#include "m8-damage.h"
static int mode_changing;
static void *primary_surface;
#endif

/* HC16xx MIPS ioctl ABI, confirmed in this card's driver.so fbdev_init:
 * DIS_GET_SCREEN_INFO at 0x4d78, HCFBIOSET_SCALE at 0x52f0.
 * SDK dis_screen_info/hcfb_scale definitions match these 12/8-byte layouts.
 * These calls query HDMI geometry and set the existing framebuffer scaler;
 * they do not configure HDMI timings, clocks, GPIO or flash memory. */
#define HC_GET_SCREEN 0xc00c0e0cUL
#define HC_SET_SCALE  0x80080d00UL
struct hc_screen { int32_t type; uint16_t x, y, width, height; };
struct hc_scale { uint16_t h_div, v_div, h_mul, v_mul; };
_Static_assert(sizeof(struct hc_screen) == 12, "screen ioctl ABI");
_Static_assert(sizeof(struct hc_scale) == 8, "scale ioctl ABI");

static int framebuffer = -1;
static unsigned frames;
static int enabled;

static int valid_geometry(const struct fb_var_screeninfo *v,
                          const struct fb_fix_screeninfo *f) {
    uint64_t last = ((uint64_t)v->yoffset + v->yres - 1) * f->line_length +
                    ((uint64_t)v->xoffset + v->xres) * (v->bits_per_pixel / 8);
    return v->xres && v->yres && v->xres <= 1920 && v->yres <= 1080 &&
           (v->bits_per_pixel == 16 || v->bits_per_pixel == 32) &&
           f->smem_len <= 32U * 1024U * 1024U && last <= f->smem_len &&
           ((uint64_t)v->xoffset + v->xres) * (v->bits_per_pixel / 8) <= f->line_length;
}

static void prepare(void) {
    struct fb_var_screeninfo v;
    struct fb_fix_screeninfo f;
    if (framebuffer < 0) framebuffer = open("/dev/fb0", O_RDWR | O_CLOEXEC);
    if (framebuffer < 0) { perror("M8Display open fb0"); return; }
    if (ioctl(framebuffer, FBIOGET_VSCREENINFO, &v) ||
        ioctl(framebuffer, FBIOGET_FSCREENINFO, &f)) { perror("M8Display query"); return; }
    if (memcmp(f.id, "hcfb", 4) || !valid_geometry(&v, &f)) {
        fprintf(stderr, "M8Display: unsupported framebuffer; no vendor ioctls issued\n");
        close(framebuffer); framebuffer = -1; enabled = 0; return;
    }
    fprintf(stderr, "M8Display: %.16s %ux%u virtual=%ux%u offset=%u,%u bpp=%u stride=%u RGB=%u/%u,%u/%u,%u/%u alpha=%u/%u\n",
            f.id, v.xres, v.yres, v.xres_virtual, v.yres_virtual,
            v.xoffset, v.yoffset, v.bits_per_pixel, f.line_length,
            v.red.offset, v.red.length, v.green.offset, v.green.length,
            v.blue.offset, v.blue.length, v.transp.offset, v.transp.length);
    int display = open("/dev/dis", O_RDWR | O_CLOEXEC);
    if (display >= 0) {
        struct hc_screen screen = {1, 0, 0, 0, 0};
        if (!ioctl(display, HC_GET_SCREEN, &screen) && screen.width && screen.height &&
            screen.width <= 4096 && screen.height <= 2160) {
            struct hc_scale scale = {(uint16_t)v.xres, (uint16_t)v.yres,
                                     screen.width, screen.height};
            int result = ioctl(framebuffer, HC_SET_SCALE, &scale);
            fprintf(stderr, "M8Display: HDMI=%ux%u scale_result=%d errno=%d\n",
                    screen.width, screen.height, result, result ? errno : 0);
        } else perror("M8Display HDMI query");
        close(display);
    } else perror("M8Display open dis");
    /* DirectFB does not perform the stock driver's final unblank sequence. */
    int blank = ioctl(framebuffer, FBIOBLANK, FB_BLANK_NORMAL);
    int unblank = ioctl(framebuffer, FBIOBLANK, FB_BLANK_UNBLANK);
    fprintf(stderr, "M8Display: blank=%d unblank=%d\n", blank, unblank);
    fflush(stderr);
    (void)fsync(STDERR_FILENO);
}

static unsigned char component(uint32_t value, struct fb_bitfield field) {
    if (!field.length || field.length > 8 || field.offset + field.length > 32) return 0;
    unsigned maximum = (1U << field.length) - 1;
    return (unsigned char)(((value >> field.offset) & maximum) * 255U / maximum);
}

static void capture(void) {
    const char *path = getenv("LILSMOKE_CAPTURE_FRAME");
    struct fb_var_screeninfo v;
    struct fb_fix_screeninfo f;
    if (!path || strncmp(path, "/lilos/lilsmoke/", 16) || framebuffer < 0 ||
        ioctl(framebuffer, FBIOGET_VSCREENINFO, &v) ||
        ioctl(framebuffer, FBIOGET_FSCREENINFO, &f) || !valid_geometry(&v, &f)) return;
    int fd = open(path, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (fd < 0) return;
    void *mapping = mmap(NULL, f.smem_len, PROT_READ, MAP_SHARED, framebuffer, 0);
    FILE *output = fdopen(fd, "wb");
    unsigned char row[1920 * 3];
    size_t nonblack = 0;
    int good = mapping != MAP_FAILED && output != NULL;
    if (good) {
        good = fprintf(output, "P6\n%u %u\n255\n", v.xres, v.yres) > 0;
        for (unsigned y = 0; y < v.yres && good; ++y) {
            const unsigned char *source = (const unsigned char *)mapping +
                ((size_t)y + v.yoffset) * f.line_length + v.xoffset * (v.bits_per_pixel / 8);
            for (unsigned x = 0; x < v.xres; ++x) {
                uint32_t pixel = 0;
                memcpy(&pixel, source + x * (v.bits_per_pixel / 8), v.bits_per_pixel / 8);
                row[x * 3] = component(pixel, v.red);
                row[x * 3 + 1] = component(pixel, v.green);
                row[x * 3 + 2] = component(pixel, v.blue);
                nonblack += !!(row[x * 3] | row[x * 3 + 1] | row[x * 3 + 2]);
            }
            good = fwrite(row, 3, v.xres, output) == v.xres;
        }
        if (fflush(output) || fsync(fd)) good = 0;
    }
    if (mapping != MAP_FAILED) munmap(mapping, f.smem_len);
    if (output) fclose(output); else close(fd);
    if (!good) unlink(path);
    fprintf(stderr, "M8Display: capture=%s success=%d nonblack_pixels=%zu\n", path, good, nonblack);
    fflush(stderr);
    (void)fsync(STDERR_FILENO);
}

static void after_present(void) {
    if (!frames) {
        const char *setting = getenv("LILSMOKE_HCFB");
        enabled = setting && !strcmp(setting, "1");
    }
    if (!enabled) { ++frames; return; }
    if (!frames || frames == 2) prepare();
    if (frames == 9) capture();
    ++frames;
}

#ifndef M8_DISPLAY_TEST
void lilsmoke_present_checkpoint(void) { after_present(); }

int lilsmoke_present_damage(const void *pixels, unsigned width, unsigned height,
                           unsigned pitch, const int32_t *rects, unsigned count) {
    if (!m8_damage_present(pixels, width, height, pitch, rects, count)) return 0;
    if (count) after_present();
    return 1;
}

void *SDL_SetVideoMode(int width, int height, int depth, uint32_t flags) {
    static void *(*original)(int, int, int, uint32_t);
    if (!original) original = (void *(*)(int, int, int, uint32_t))dlsym(RTLD_NEXT, "SDL_SetVideoMode");
    m8_native_video_reset();
    mode_changing = 1;
    void *surface = original ? original(width, height, depth, flags) : NULL;
    mode_changing = 0;
    primary_surface = surface;
    const char *setting = getenv("LILSMOKE_HCFB");
    const char *path = getenv("LILSMOKE_CAPTURE_FRAME");
    /* Capture the running native SNES mode, not the earlier loading/menu
     * frames. Only the dedicated generated PocketSNES capture is replaced. */
    if (surface && width == 256 && (height == 224 || height == 239) &&
        setting && !strcmp(setting, "1") && path &&
        !strcmp(path, "/lilos/lilsmoke/pocketsnes-frame.ppm")) {
        frames = 0;
        (void)unlink(path);
    }
    return surface;
}
void SDL_RenderPresent(void *renderer) {
    static void (*original)(void *);
    if (!original) original = (void (*)(void *))dlsym(RTLD_NEXT, "SDL_RenderPresent");
    if (original) original(renderer);
    after_present();
}
int SDL_UpdateWindowSurface(void *window) {
    static int (*original)(void *);
    if (!original) original = (int (*)(void *))dlsym(RTLD_NEXT, "SDL_UpdateWindowSurface");
    int result = original ? original(window) : -1;
    after_present();
    return result;
}
/* SDL12-compat resolves SDL2 calls on its own library handle, bypassing a
 * preloaded SDL2 symbol. Intercept the public SDL 1.2 calls as well. */
int SDL_Flip(void *surface) {
    static int (*original)(void *);
    if (!original) original = (int (*)(void *))dlsym(RTLD_NEXT, "SDL_Flip");
    int native = !mode_changing && (m8_od_present(surface) || m8_native_present(surface));
    int result = native ? 0 : (original ? original(surface) : -1);
    after_present();
    return result;
}
void SDL_Quit(void) {
    static void (*original)(void);
    if (!original) original = (void (*)(void))dlsym(RTLD_NEXT, "SDL_Quit");
    m8_native_video_reset();
    lilsmoke_damage_reset();
    primary_surface = NULL;
    if (original) original();
}
void SDL_UpdateRects(void *surface, int count, void *rectangles) {
    static void (*original)(void *, int, void *);
    if (!original) original = (void (*)(void *, int, void *))dlsym(RTLD_NEXT, "SDL_UpdateRects");
    if (mode_changing || surface != primary_surface || !m8_od_present(surface))
        if (original) original(surface, count, rectangles);
    after_present();
}
void SDL_UpdateRect(void *surface, int x, int y, uint32_t width, uint32_t height) {
    static void (*original)(void *, int, int, uint32_t, uint32_t);
    if (!original) original = (void (*)(void *, int, int, uint32_t, uint32_t))dlsym(RTLD_NEXT, "SDL_UpdateRect");
    if (mode_changing || surface != primary_surface || !m8_od_present(surface))
        if (original) original(surface, x, y, width, height);
    after_present();
}
#endif
