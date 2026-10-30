#define _GNU_SOURCE
#include "m8-boot-logo.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <signal.h>

uint64_t m8_boot_now(void) {
    struct timespec t;
    if (clock_gettime(CLOCK_MONOTONIC, &t)) return 0;
    return (uint64_t)t.tv_sec * 1000U + (unsigned long)t.tv_nsec / 1000000U;
}

uint64_t m8_boot_remaining(uint64_t start, uint64_t now) {
    if (!start || now < start || start > UINT64_MAX - M8_BOOT_MIN_MS) return 0;
    /* The timestamps are millisecond floors; one guard millisecond ensures
     * the visible interval is never rounded below six actual seconds. */
    return now - start <= M8_BOOT_MIN_MS ? M8_BOOT_MIN_MS + 1U - (now - start) : 0;
}

static int fields(const struct fb_var_screeninfo *v) {
    const struct fb_bitfield *b[] = {&v->red, &v->green, &v->blue, &v->transp};
    uint32_t used = 0;
    if (v->bits_per_pixel != 16 && v->bits_per_pixel != 32) return 0;
    for (unsigned i = 0; i < 4; ++i) {
        if ((!b[i]->length && i < 3) || b[i]->length > 8 || b[i]->msb_right ||
            b[i]->offset > v->bits_per_pixel || b[i]->length > v->bits_per_pixel - b[i]->offset) return 0;
        uint32_t mask = b[i]->length ? ((1U << b[i]->length) - 1U) << b[i]->offset : 0;
        if (used & mask) return 0;
        used |= mask;
    }
    return !v->grayscale && !v->nonstd && !v->rotate;
}

int m8_boot_geometry(const struct fb_var_screeninfo *v, const struct fb_fix_screeninfo *f) {
    if (memcmp(f->id, "hcfb", 4) || !fields(v) || !v->xres || !v->yres ||
        v->xres > 1920 || v->yres > 1080 || v->xres_virtual > 4096 ||
        v->yres_virtual > 4320 || !f->smem_len || f->smem_len > M8_BOOT_MAX_MAP ||
        (uint64_t)v->xoffset + v->xres > v->xres_virtual ||
        (uint64_t)v->yoffset + v->yres > v->yres_virtual) return 0;
    uint64_t end_x = ((uint64_t)v->xoffset + v->xres) * (v->bits_per_pixel / 8U);
    uint64_t last = ((uint64_t)v->yoffset + v->yres - 1U) * f->line_length + end_x;
    return end_x <= f->line_length && last <= f->smem_len;
}

int m8_boot_mode(const struct fb_var_screeninfo *v, uint32_t memory, uint32_t *stride) {
    struct fb_fix_screeninfo f = {.id="hcfb", .smem_len=memory};
    if (v->xres_virtual > 4096) return 0;
    f.line_length = v->xres_virtual * (v->bits_per_pixel / 8U);
    if (!m8_boot_geometry(v, &f) || (uint64_t)f.line_length * v->yres_virtual > memory) return 0;
    *stride = f.line_length;
    return 1;
}

int m8_boot_read_state(const char *path, uint64_t *start) {
    char data[96], extra;
    unsigned long long value;
    struct stat s;
    int fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
    if (fd < 0) return 0;
    ssize_t n = -1;
    if (!fstat(fd, &s) && S_ISREG(s.st_mode) && s.st_uid == geteuid() &&
        !(s.st_mode & 0022) && s.st_size > 0 && s.st_size < (off_t)sizeof(data))
        n = read(fd, data, sizeof(data) - 1);
    close(fd);
    if (n <= 0 || n != s.st_size) return 0;
    data[n] = 0;
    if (sscanf(data, "M8_BOOT_LOGO_V1 %llu %c", &value, &extra) != 1 || !value ||
        value > m8_boot_now() || value > UINT64_MAX - M8_BOOT_MIN_MS) return 0;
    *start = value;
    return 1;
}

static uint32_t channel(unsigned value, struct fb_bitfield b) {
    return b.length ? ((value * ((1U << b.length) - 1U) + 127U) / 255U) << b.offset : 0;
}

int m8_boot_draw(void *map, const struct fb_var_screeninfo *v,
                 const struct fb_fix_screeninfo *f, unsigned screen_w, unsigned screen_h,
                 const unsigned char *argb, unsigned width, unsigned height) {
    if (!map || !argb || !width || !height || width > 1280 || height > 720 ||
        !screen_w || !screen_h || screen_w > 4096 || screen_h > 2160 || !m8_boot_geometry(v, f)) return 0;
    /* Center above a proportional progress area; tiny modes have no bar. */
    unsigned artwork_h = v->yres;
    if (v->xres >= 160U && v->yres >= 120U) artwork_h = v->yres * 4U / 5U;
    unsigned w = v->xres * 9U / 10U, h = artwork_h * 9U / 10U;
    uint64_t want_w = (uint64_t)h * width * screen_h * v->xres /
                         ((uint64_t)height * screen_w * v->yres);
    if (want_w <= w) w = (unsigned)want_w;
    else h = (unsigned)((uint64_t)w * height * screen_w * v->yres /
                               ((uint64_t)width * screen_h * v->xres));
    if (!w || !h) return 0;
    unsigned left = (v->xres - w) / 2U, top = (artwork_h - h) / 2U;
    unsigned bytes = v->bits_per_pixel / 8U;
    uint32_t black = channel(255, v->transp);
    for (unsigned y = 0; y < v->yres; ++y) {
        unsigned char *row = (unsigned char *)map + ((size_t)y + v->yoffset) * f->line_length + v->xoffset * bytes;
        for (unsigned x = 0; x < v->xres; ++x) {
            uint32_t pixel = black;
            if (x >= left && x - left < w && y >= top && y - top < h) {
                unsigned sx = (uint64_t)(x - left) * width / w;
                unsigned sy = (uint64_t)(y - top) * height / h;
                const unsigned char *p = argb + ((size_t)sy * width + sx) * 4U;
                pixel |= channel(p[2], v->red) | channel(p[1], v->green) | channel(p[0], v->blue);
            }
            memcpy(row + x * bytes, &pixel, bytes);
        }
    }
    return 1;
}

static void progress_box(void *map, const struct fb_var_screeninfo *v,
                         const struct fb_fix_screeninfo *f, unsigned x, unsigned y,
                         unsigned width, unsigned height, unsigned rgb) {
    uint32_t pixel = channel((rgb >> 16) & 255, v->red) |
                     channel((rgb >> 8) & 255, v->green) |
                     channel(rgb & 255, v->blue) | channel(255, v->transp);
    unsigned bytes = v->bits_per_pixel / 8U;
    for (unsigned yy = y; yy < v->yres && yy - y < height; ++yy) {
        unsigned char *row = (unsigned char *)map + (yy + v->yoffset) * (size_t)f->line_length;
        for (unsigned xx = x; xx < v->xres && xx - x < width; ++xx)
            memcpy(row + (xx + v->xoffset) * bytes, &pixel, bytes);
    }
}

int m8_boot_progress(void *map, const struct fb_var_screeninfo *v,
                     const struct fb_fix_screeninfo *f, unsigned percent) {
    static const unsigned char digits[11][7] = {
        {14,17,19,21,25,17,14}, {4,12,4,4,4,4,14}, {14,17,1,2,4,8,31},
        {30,1,1,14,1,1,30}, {2,6,10,18,31,2,2}, {31,16,16,30,1,1,30},
        {14,16,16,30,17,17,14}, {31,1,2,4,8,8,8}, {14,17,17,14,17,17,14},
        {14,17,17,15,1,1,14}, {17,18,2,4,8,9,17}
    };
    if (!map || percent > 100 || !m8_boot_geometry(v, f) || v->xres < 160 || v->yres < 120) return 0;
    unsigned scale = v->yres / 240U;
    if (!scale) scale = 1;
    unsigned x = v->xres / 5U, width = v->xres * 3U / 5U;
    unsigned y = v->yres * 86U / 100U, height = scale * 4U;
    progress_box(map, v, f, x, y, width, v->yres - y, 0);
    progress_box(map, v, f, x, y, width, height, 0x303840);
    progress_box(map, v, f, x, y, width * percent / 100U, height, 0x2697ed);
    char text[5];
    snprintf(text, sizeof(text), "%u%%", percent);
    unsigned length = (unsigned)strlen(text);
    x = (v->xres - length * 6U * scale) / 2U;
    y += height + scale * 3U;
    for (unsigned i = 0; i < length; ++i) {
        unsigned glyph = text[i] == '%' ? 10U : (unsigned)(text[i] - '0');
        for (unsigned yy = 0; yy < 7; ++yy) for (unsigned xx = 0; xx < 5; ++xx)
            if (digits[glyph][yy] & (16U >> xx))
                progress_box(map, v, f, x + (i * 6U + xx) * scale, y + yy * scale,
                             scale, scale, 0xffffff);
    }
    return 1;
}

#ifndef M8_BOOT_CORE_ONLY
static uint32_t little32(const unsigned char *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static volatile sig_atomic_t stop_loading;
static void loading_signal(int number) { (void)number; stop_loading = 1; }

static int progress_value(const char *dir) {
    char path[256], data[96];
    if (snprintf(path, sizeof(path), "%s/progress", dir) >= (int)sizeof(path)) return -1;
    int fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
    if (fd < 0) return -1;
    struct stat s;
    ssize_t n = -1;
    if (!fstat(fd, &s) && S_ISREG(s.st_mode) && s.st_uid == geteuid() &&
        !(s.st_mode & 0022) && s.st_size > 0 && s.st_size < (off_t)sizeof(data))
        n = read(fd, data, sizeof(data) - 1U);
    close(fd);
    if (n <= 0) return -1;
    data[n] = 0;
    char *end;
    unsigned long value = strtoul(data, &end, 10);
    return end != data && (*end == '\t' || *end == '\n') && value <= 100 ? (int)value : -1;
}

int main(int argc, char **argv) {
    if (argc == 2 && !strcmp(argv[1], "--self-check")) {
        puts("M8_BOOT_LOGO version=1 minimum_ms=6000"); return 0;
    }
    const char *hold = NULL;
    int minimum = 1;
    pid_t owner = 0;
    for (int i = 4; i < argc; ++i) {
        if (!strcmp(argv[i], "--hold") && i + 1 < argc) hold = argv[++i];
        else if (!strcmp(argv[i], "--no-minimum")) minimum = 0;
        else if (!strcmp(argv[i], "--owner") && i + 1 < argc) {
            char *end;
            long value = strtol(argv[++i], &end, 10);
            if (*end || value < 2 || value > 4194304) return 2;
            owner = (pid_t)value;
        } else return 2;
    }
    if (argc < 4 || strcmp(argv[1], "--show") || (hold && strlen(hold) > 200U)) {
        fprintf(stderr, "usage: m8-boot-logo --show boot-logo.argb private-state-path [--hold RAM-dir] [--no-minimum] [--owner PID]\n"); return 2;
    }
    int result = 1, fd = -1, display = -1, state = -1;
    unsigned char header[16], *pixels = NULL;
    void *map = MAP_FAILED;
    size_t span = 0;
    FILE *image = fopen(argv[2], "rb");
    if (!image || fread(header, 1, sizeof(header), image) != sizeof(header) ||
        memcmp(header, "M8ARGB1\0", 8)) goto done;
    unsigned w = little32(header + 8), h = little32(header + 12);
    if (!w || !h || w > 1280 || h > 720) goto done;
    size_t size = (size_t)w * h * 4U;
    pixels = malloc(size);
    if (!pixels || fread(pixels, 1, size, image) != size || fgetc(image) != EOF) goto done;
    fd = open("/dev/fb0", O_RDWR | O_CLOEXEC);
    struct fb_var_screeninfo v;
    struct fb_fix_screeninfo f;
    if (fd < 0 || ioctl(fd, FBIOGET_VSCREENINFO, &v) || ioctl(fd, FBIOGET_FSCREENINFO, &f) ||
        !m8_boot_geometry(&v, &f)) goto done;
    /* Stock can report a valid mode before its display plane has actually
     * been committed. Initialize it just as the working native presenter does. */
    v.xres_virtual = v.xres;
    v.yres_virtual = v.yres;
    v.xoffset = v.yoffset = 0;
    v.activate = FB_ACTIVATE_NOW;
    if (v.bits_per_pixel == 32) {
        v.red = (struct fb_bitfield){16,8,0};
        v.green = (struct fb_bitfield){8,8,0};
        v.blue = (struct fb_bitfield){0,8,0};
        v.transp = (struct fb_bitfield){24,8,0};
        v.grayscale = v.nonstd = v.rotate = 0;
    }
    if (ioctl(fd, FBIOPUT_VSCREENINFO, &v) || ioctl(fd, FBIOGET_VSCREENINFO, &v) ||
        ioctl(fd, FBIOGET_FSCREENINFO, &f) || !m8_boot_geometry(&v, &f)) goto done;
    display = open("/dev/dis", O_RDWR | O_CLOEXEC);
    struct m8_boot_screen screen = {.type=1};
    if (display < 0 || ioctl(display, 0xc00c0e0cU, &screen) || !screen.width || !screen.height ||
        screen.width > 4096 || screen.height > 2160) goto done;
    struct m8_boot_scale scale = {v.xres, v.yres, screen.width, screen.height};
    span = ((size_t)v.yoffset + v.yres - 1U) * f.line_length + ((size_t)v.xoffset + v.xres) * (v.bits_per_pixel / 8U);
    if (ioctl(fd, M8_BOOT_CACHE, 0UL)) goto done;
    map = mmap(NULL, span, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (map == MAP_FAILED || !m8_boot_draw(map, &v, &f, screen.width, screen.height, pixels, w, h)) goto done;
    __sync_synchronize();
    /* Match the proven M8 display adapter: an unblank alone can leave the
     * stock HDMI layer latched blank even when the ioctl succeeds. */
    if (ioctl(fd, M8_BOOT_SCALE, &scale) || ioctl(fd, FBIOPAN_DISPLAY, &v) ||
        ioctl(fd, FBIOBLANK, FB_BLANK_NORMAL) ||
        ioctl(fd, FBIOBLANK, FB_BLANK_UNBLANK)) goto done;
    uint64_t start = m8_boot_now();
    if (!start) goto done;
    state = open(argv[3], O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (state < 0 || dprintf(state, "M8_BOOT_LOGO_V1 %llu\n", (unsigned long long)start) < 0) goto done;
    fprintf(stderr, "M8BootLogo submitted start_ms=%llu source=%ux%u framebuffer=%ux%u screen=%ux%u bpp=%u alpha=%u/%u mode_committed=1 pan_committed=1 minimum_ms=6000\n",
            (unsigned long long)start, w, h, v.xres, v.yres, screen.width, screen.height,
            v.bits_per_pixel, v.transp.offset, v.transp.length);
    free(pixels); pixels = NULL;
    fclose(image); image = NULL;
    /* Keep the committed plane and mapping owned for the visible minimum.
     * The first-frame gate protects it during subsequent UI initialization. */
    if (hold) {
        char active[256];
        snprintf(active, sizeof(active), "%s/active", hold);
        signal(SIGTERM, loading_signal);
        signal(SIGINT, loading_signal);
        int previous = -1;
        while (!stop_loading && !access(active, F_OK) && (!owner || !kill(owner, 0) || errno == EPERM)) {
            int percent = progress_value(hold);
            if (percent >= 0 && percent != previous) {
                (void)m8_boot_progress(map, &v, &f, (unsigned)percent);
                __sync_synchronize();
                (void)ioctl(fd, FBIOPAN_DISPLAY, &v);
                previous = percent;
            }
            struct timespec delay = {0, 100000000L};
            (void)nanosleep(&delay, NULL);
        }
    } else if (minimum) {
        uint64_t remaining;
        while ((remaining = m8_boot_remaining(start, m8_boot_now())) != 0) {
            struct timespec delay = {(time_t)(remaining / 1000U),
                                     (long)(remaining % 1000U) * 1000000L};
            if (nanosleep(&delay, NULL) && errno != EINTR) goto done;
        }
    }
    fprintf(stderr, "M8BootLogo %s elapsed_ms=%llu\n", hold ? "loading_hold_complete" : "minimum_hold_complete",
            (unsigned long long)(m8_boot_now() - start));
    result = 0;
done:
    if (result) fprintf(stderr, "M8BootLogo unavailable errno=%d; normal boot retained\n", errno);
    if (state >= 0) { close(state); if (result) unlink(argv[3]); }
    if (map != MAP_FAILED) munmap(map, span);
    if (display >= 0) close(display);
    if (fd >= 0) close(fd);
    if (image) fclose(image);
    free(pixels);
    if (hold) {
        char stopped[256];
        snprintf(stopped, sizeof(stopped), "%s/stopped", hold);
        int ack = open(stopped, O_CREAT | O_WRONLY | O_CLOEXEC | O_NOFOLLOW, 0600);
        if (ack >= 0) close(ack);
    }
    return result;
}
#endif
