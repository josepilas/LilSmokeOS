#define _GNU_SOURCE
#include "m8-boot-logo.h"
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#ifdef __GLIBC__
typedef unsigned long ioctl_request;
#else
typedef int ioctl_request;
#endif
static int (*next_ioctl)(int, ioctl_request, ...);
static void *(*next_mmap)(void *, size_t, int, int, int, off_t);
static int (*next_munmap)(void *, size_t);
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static atomic_int active;
static struct {
    int fd;
    dev_t device;
    uint64_t start;
    struct fb_var_screeninfo original, pending;
    struct fb_fix_screeninfo fixed;
    struct m8_boot_scale scale;
    int have_scale;
    void *shadow;
    size_t length;
    int prot;
    int loading, game;
} gate = {.fd=-1};

static int is_framebuffer(int fd) {
    struct stat s;
    return !fstat(fd, &s) && S_ISCHR(s.st_mode) && s.st_rdev == gate.device;
}

/* Only the one cold-boot UI invocation receives this token. Applications,
 * recovery and later UI invocations retain the original display adapter. */
#ifndef M8_BOOT_GATE_TEST
__attribute__((constructor))
#endif
static void boot_gate_init(void) {
    next_ioctl = dlsym(RTLD_NEXT, "ioctl");
    next_mmap = dlsym(RTLD_NEXT, "mmap");
    next_munmap = dlsym(RTLD_NEXT, "munmap");
    const char *state = getenv("LILSMOKE_M8_BOOT_STATE");
    int requested = state && !strcmp(state, M8_BOOT_STATE);
    const char *game = getenv("LILSMOKE_M8_LOADING_GAME");
    gate.game = game && !strcmp(game, "1");
    // A game passes through dynamic routers before its final exec. Preserve
    // the token until a display actually presents, not merely until libc loads.
    if (!gate.game) unsetenv("LILSMOKE_M8_BOOT_STATE");
    if (!requested || !next_ioctl || !next_mmap || !next_munmap ||
        !m8_boot_read_state(M8_BOOT_STATE, &gate.start)) return;
    struct stat s;
    gate.fd = open("/dev/fb0", O_RDWR | O_CLOEXEC);
    if (gate.fd < 0 || fstat(gate.fd, &s) || !S_ISCHR(s.st_mode) ||
        next_ioctl(gate.fd, FBIOGET_VSCREENINFO, &gate.original) ||
        next_ioctl(gate.fd, FBIOGET_FSCREENINFO, &gate.fixed) ||
        !m8_boot_geometry(&gate.original, &gate.fixed)) {
        if (gate.fd >= 0) close(gate.fd);
        gate.fd = -1;
        fprintf(stderr, "M8BootLogo gate unavailable; normal boot retained\n");
        return;
    }
    gate.device = s.st_rdev;
    gate.pending = gate.original;
    const char *loading = getenv("LILSMOKE_M8_LOADING_DIR");
    gate.loading = loading && !strcmp(loading, M8_LOADING_DIR);
    atomic_store(&active, 1);
    fprintf(stderr, "M8BootLogo holding scanout while DirectFB/UI initialize\n");
}

int ioctl(int fd, ioctl_request request, ...) {
    va_list ap;
    va_start(ap, request);
    void *argument = va_arg(ap, void *);
    va_end(ap);
    if (!next_ioctl) next_ioctl = dlsym(RTLD_NEXT, "ioctl");
    if (!next_ioctl) { errno = ENOSYS; return -1; }
    if (!atomic_load(&active) || !is_framebuffer(fd)) return next_ioctl(fd, request, argument);
    pthread_mutex_lock(&lock);
    int result = 0;
    if (!atomic_load(&active)) result = next_ioctl(fd, request, argument);
    else switch ((uint32_t)request) {
        case FBIOGET_VSCREENINFO: *(struct fb_var_screeninfo *)argument = gate.pending; break;
        case FBIOGET_FSCREENINFO: *(struct fb_fix_screeninfo *)argument = gate.fixed; break;
        case FBIOPUT_VSCREENINFO: {
            struct fb_var_screeninfo v = *(struct fb_var_screeninfo *)argument;
            uint32_t stride;
            if (!m8_boot_mode(&v, gate.fixed.smem_len, &stride)) { errno = EINVAL; result = -1; break; }
            if ((v.activate & FB_ACTIVATE_MASK) == FB_ACTIVATE_TEST) {
                result = next_ioctl(fd, request, argument);
            } else {
                gate.pending = v;
                gate.fixed.line_length = stride;
            }
            break;
        }
        case FBIOPAN_DISPLAY: {
            struct fb_var_screeninfo v = gate.pending;
            v.xoffset = ((struct fb_var_screeninfo *)argument)->xoffset;
            v.yoffset = ((struct fb_var_screeninfo *)argument)->yoffset;
            if (!m8_boot_geometry(&v, &gate.fixed)) { errno = EINVAL; result = -1; }
            else { gate.pending.xoffset = v.xoffset; gate.pending.yoffset = v.yoffset; }
            break;
        }
        case M8_BOOT_SCALE: gate.scale = *(struct m8_boot_scale *)argument; gate.have_scale = 1; break;
        case M8_BOOT_CACHE:
        case FBIOBLANK:
        case FBIOPUTCMAP:
        case FBIO_WAITFORVSYNC: break;
        case FBIOGETCMAP:
        case FBIOGET_CON2FBMAP:
        case FBIOGET_VBLANK: result = next_ioctl(fd, request, argument); break;
        default: errno = ENOTSUP; result = -1; break;
    }
    pthread_mutex_unlock(&lock);
    return result;
}

void *mmap(void *address, size_t length, int prot, int flags, int fd, off_t offset) {
    if (!next_mmap) next_mmap = dlsym(RTLD_NEXT, "mmap");
    if (!next_mmap) { errno = ENOSYS; return MAP_FAILED; }
    if (!atomic_load(&active) || !is_framebuffer(fd)) return next_mmap(address, length, prot, flags, fd, offset);
    pthread_mutex_lock(&lock);
    void *result;
    if (!atomic_load(&active)) result = next_mmap(address, length, prot, flags, fd, offset);
    else if (gate.shadow || offset || !length || length > gate.fixed.smem_len ||
             (flags & MAP_FIXED) || (prot & PROT_EXEC) || (flags & MAP_TYPE) != MAP_SHARED) {
        errno = ENOTSUP; result = MAP_FAILED;
    } else {
        /* Reserve through the framebuffer first: MIPS shared mappings need
         * a cache-colour-aligned VA, not just an anonymous page-aligned VA.
         * Replacing the untouched mapping keeps the logo visible, while the
         * later MAP_SHARED|MAP_FIXED handoff uses a driver-approved address. */
        result = next_mmap(NULL, length, prot, MAP_SHARED, fd, 0);
        if (result != MAP_FAILED) {
            void *shadow = next_mmap(result, length, prot,
                                    MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
            if (shadow == MAP_FAILED) {
                int error = errno;
                next_munmap(result, length);
                result = MAP_FAILED;
                errno = error;
            } else {
                gate.shadow = result; gate.length = length; gate.prot = prot;
            }
        }
    }
    pthread_mutex_unlock(&lock);
    return result;
}

int munmap(void *address, size_t length) {
    if (!next_munmap) next_munmap = dlsym(RTLD_NEXT, "munmap");
    if (!next_munmap) { errno = ENOSYS; return -1; }
    if (!atomic_load(&active)) return next_munmap(address, length);
    pthread_mutex_lock(&lock);
    if (gate.shadow && (uintptr_t)address < (uintptr_t)gate.shadow + gate.length &&
        (uintptr_t)gate.shadow < (uintptr_t)address + length) {
        if (address != gate.shadow || length != gate.length) {
            pthread_mutex_unlock(&lock); errno = ENOTSUP; return -1;
        }
        gate.shadow = NULL; gate.length = 0;
    }
    int result = next_munmap(address, length);
    pthread_mutex_unlock(&lock);
    return result;
}

static void wait_minimum(void) {
    if (gate.game) return;
    uint64_t remaining;
    while ((remaining = m8_boot_remaining(gate.start, m8_boot_now())) != 0) {
        if (atomic_load(&active) && access(M8_BOOT_STATE, F_OK)) {
            fprintf(stderr, "M8BootLogo cancelled by supervisor; no shutdown delay\n");
            _exit(143);
        }
        struct timespec t = {(time_t)(remaining / 1000U), (long)(remaining % 1000U) * 1000000L};
        if (nanosleep(&t, NULL) && errno != EINTR) break;
    }
}

static void stop_loading_helper(void) {
    if (!gate.loading) return;
    (void)unlink(M8_LOADING_DIR "/active");
    // The helper acknowledges only after it stops touching/unmaps scanout.
    // Bound this optional handshake so a missing helper cannot stall a game.
    for (unsigned i = 0; i < 50 && access(M8_LOADING_DIR "/stopped", F_OK); ++i) {
        struct timespec delay = {0, 10000000L};
        (void)nanosleep(&delay, NULL);
    }
}

static void handoff_failed(const char *stage) {
    fprintf(stderr, "M8BootLogo handoff failed stage=%s errno=%d; retrying without logo gate\n", stage, errno);
    (void)next_ioctl(gate.fd, FBIOPUT_VSCREENINFO, &gate.original);
    (void)next_ioctl(gate.fd, FBIOBLANK, (void *)(uintptr_t)FB_BLANK_UNBLANK);
    /* Never retry this optional gate after a failed handoff. */
    unlink(M8_BOOT_STATE);
    stop_loading_helper();
    /* Do not leave DirectFB believing a private mapping is physical scanout. */
    _exit(70);
}

static void handoff(int sdl_frame) {
    if (!atomic_load(&active)) return;
    wait_minimum();
    pthread_mutex_lock(&lock);
    if (!atomic_load(&active)) { pthread_mutex_unlock(&lock); return; }
    stop_loading_helper();
    gate.pending.activate = FB_ACTIVATE_NOW;
    if (next_ioctl(gate.fd, FBIOPUT_VSCREENINFO, &gate.pending)) handoff_failed("mode");
    struct fb_var_screeninfo v;
    struct fb_fix_screeninfo f;
    if (next_ioctl(gate.fd, FBIOGET_VSCREENINFO, &v) || next_ioctl(gate.fd, FBIOGET_FSCREENINFO, &f) ||
        !m8_boot_geometry(&v, &f) || v.xres != gate.pending.xres || v.yres != gate.pending.yres ||
        v.xres_virtual != gate.pending.xres_virtual || v.yres_virtual != gate.pending.yres_virtual ||
        v.xoffset != gate.pending.xoffset || v.yoffset != gate.pending.yoffset ||
        v.bits_per_pixel != gate.pending.bits_per_pixel || f.line_length != gate.fixed.line_length ||
        memcmp(&v.red, &gate.pending.red, sizeof(v.red)) || memcmp(&v.green, &gate.pending.green, sizeof(v.green)) ||
        memcmp(&v.blue, &gate.pending.blue, sizeof(v.blue)) || memcmp(&v.transp, &gate.pending.transp, sizeof(v.transp)))
        handoff_failed("mode-readback");
    if (next_ioctl(gate.fd, M8_BOOT_CACHE, (void *)0)) handoff_failed("uncached-map");
    if (gate.shadow) {
        if (gate.length > f.smem_len) handoff_failed("map-size");
        /* DirectFB's video heap follows scanout in this same mapping. Keep
         * every surface/texture, including a native first-frame source that
         * aliases the shadow, before replacing its backing at the same VA. */
        void *real = next_mmap(NULL, gate.length, PROT_READ | PROT_WRITE, MAP_SHARED, gate.fd, 0);
        if (real == MAP_FAILED) handoff_failed("copy-map");
        memcpy(real, gate.shadow, gate.length);
        next_munmap(real, gate.length);
        if (next_mmap(gate.shadow, gate.length, gate.prot, MAP_SHARED | MAP_FIXED, gate.fd, 0) != gate.shadow)
            handoff_failed("primary-map");
    }
    if (gate.have_scale && next_ioctl(gate.fd, M8_BOOT_SCALE, &gate.scale)) handoff_failed("scale");
    (void)next_ioctl(gate.fd, FBIOPAN_DISPLAY, &gate.pending);
    (void)next_ioctl(gate.fd, FBIOBLANK, (void *)(uintptr_t)FB_BLANK_UNBLANK);
    atomic_store(&active, 0);
    close(gate.fd); gate.fd = -1;
    unlink(M8_BOOT_STATE);
    unsetenv("LILSMOKE_M8_BOOT_STATE");
    unsetenv("LILSMOKE_M8_LOADING_GAME");
    fprintf(stderr, "M8BootLogo first_frame_ready elapsed_ms=%llu handoff=%s\n",
            (unsigned long long)(m8_boot_now() - gate.start), sdl_frame ? "SDL-software" : "native-full-frame");
    pthread_mutex_unlock(&lock);
}

static int frame_has_color(const void *pixels, unsigned width, unsigned height,
                           unsigned pitch, unsigned bytes, uint32_t mask) {
    for (unsigned y = 0; y < height; ++y) {
        const unsigned char *row = (const unsigned char *)pixels + (size_t)y * pitch;
        for (unsigned x = 0; x < width; ++x) {
            uint32_t pixel = 0;
            memcpy(&pixel, row + x * bytes, bytes);
            if (pixel & mask) return 1;
        }
    }
    return 0;
}

static int pending_frame_ready(void) {
    if (!gate.shadow) return 1;
    const struct fb_var_screeninfo *v = &gate.pending;
    if (!m8_boot_geometry(v, &gate.fixed)) return 0;
    unsigned bytes = v->bits_per_pixel / 8U;
    size_t start = (size_t)v->yoffset * gate.fixed.line_length + v->xoffset * bytes;
    size_t end = start + (size_t)(v->yres - 1U) * gate.fixed.line_length + v->xres * bytes;
    if (end > gate.length) return 0;
    uint32_t mask = ((1U << v->red.length) - 1U) << v->red.offset |
                    ((1U << v->green.length) - 1U) << v->green.offset |
                    ((1U << v->blue.length) - 1U) << v->blue.offset;
    return frame_has_color((const unsigned char *)gate.shadow + start, v->xres,
                           v->yres, gate.fixed.line_length, bytes, mask);
}

/* Native games and RGUI create a second, small RGB565 scanout mapping.
 * Release the protected DirectFB mapping before that mode switch, not after
 * an SDL fallback has already disabled the native presentation path. */
int lilsmoke_loading_prepare_rgb(const void *pixels, unsigned width,
                                unsigned height, unsigned pitch) {
    if (!atomic_load(&active)) return 0;
    if (!pixels || !width || !height || width > 640 || height > 480 ||
        pitch < width * 2U || pitch > 65535) return 0;
    if (!frame_has_color(pixels, width, height, pitch, 2, UINT32_C(0xffff))) return 1;
    handoff(0);
    return 0;
}

int lilsmoke_present_damage(const void *pixels, unsigned width, unsigned height,
                           unsigned pitch, const int32_t *rects, unsigned count) {
    static int (*present)(const void *, unsigned, unsigned, unsigned, const int32_t *, unsigned);
    if (!present) present = dlsym(RTLD_NEXT, "lilsmoke_present_damage");
    if (!present) return 0;
    if (atomic_load(&active)) {
        if (!pixels || !width || !height || width > 1920 || height > 1080 ||
            pitch < width * 4U || pitch > 16384 || !count || count > 2048 || !rects) return 0;
        for (unsigned i = 0; i < count; ++i) {
            const int32_t *r = rects + i * 4U;
            if (r[0] < 0 || r[1] < 0 || r[2] <= 0 || r[3] <= 0 ||
                (uint64_t)r[0] + r[2] > width || (uint64_t)r[1] + r[3] > height) return 0;
        }
        if (!frame_has_color(pixels, width, height, pitch, 4, UINT32_C(0x00ffffff))) return 1;
        handoff(0);
        /* The first UI frame replaces every logo pixel, even if the caller
         * supplies a small damage rectangle or has a previously valid shadow. */
        const int32_t full[4] = {0, 0, (int32_t)width, (int32_t)height};
        return present(pixels, width, height, pitch, full, 1);
    }
    return present(pixels, width, height, pitch, rects, count);
}

void SDL_RenderPresent(void *renderer) {
    static void (*present)(void *);
    if (!present) present = dlsym(RTLD_NEXT, "SDL_RenderPresent");
    if (!present) return;
    /* Flush the software renderer into the private framebuffer first. */
    present(renderer);
    if (atomic_load(&active) && pending_frame_ready()) handoff(1);
}

int SDL_UpdateWindowSurface(void *window) {
    static int (*present)(void *);
    if (!present) present = dlsym(RTLD_NEXT, "SDL_UpdateWindowSurface");
    int result = present ? present(window) : -1;
    if (!result && atomic_load(&active) && pending_frame_ready()) handoff(1);
    return result;
}

int SDL_UpdateWindowSurfaceRects(void *window, const void *rects, int count) {
    static int (*present)(void *, const void *, int);
    if (!present) present = dlsym(RTLD_NEXT, "SDL_UpdateWindowSurfaceRects");
    int result = present ? present(window, rects, count) : -1;
    if (!result && count > 0 && atomic_load(&active) && pending_frame_ready()) handoff(1);
    return result;
}

int SDL_Flip(void *surface) {
    static int (*present)(void *);
    if (!present) present = dlsym(RTLD_NEXT, "SDL_Flip");
    int result = present ? present(surface) : -1;
    if (!result && atomic_load(&active) && pending_frame_ready()) handoff(1);
    return result;
}

void SDL_UpdateRects(void *surface, int count, void *rects) {
    static void (*present)(void *, int, void *);
    if (!present) present = dlsym(RTLD_NEXT, "SDL_UpdateRects");
    if (present) {
        present(surface, count, rects);
        if (count > 0 && atomic_load(&active) && pending_frame_ready()) handoff(1);
    }
}

void SDL_UpdateRect(void *surface, int x, int y, unsigned w, unsigned h) {
    static void (*present)(void *, int, int, unsigned, unsigned);
    if (!present) present = dlsym(RTLD_NEXT, "SDL_UpdateRect");
    if (present) {
        present(surface, x, y, w, h);
        if (atomic_load(&active) && pending_frame_ready()) handoff(1);
    }
}
