/* File-backed hcfb double for exercising the production preload libraries.
 * No physical device is opened and no firmware ioctl reaches the kernel. */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <unistd.h>
#include "../../board/segam-m8-v6-stock/m8-boot-logo.h"
#ifdef __GLIBC__
typedef unsigned long request_type;
#else
typedef int request_type;
#endif
static unsigned char descriptors[1024];
static struct fb_var_screeninfo current;
static struct fb_fix_screeninfo fixed;
static int initialized;
static int mode_committed, pan_committed;
static uint32_t *surface;
static uint32_t color = UINT32_C(0xffaabbcc);

void *mmap(void *address, size_t length, int prot, int flags, int fd, off_t offset) {
    static void *(*real_map)(void *, size_t, int, int, int, off_t);
    if (!real_map) real_map=dlsym(RTLD_NEXT,"mmap");
    if (fd >= 0 && fd < (int)sizeof(descriptors) && descriptors[fd] == 1 &&
        (flags & MAP_FIXED) && getenv("M8_TEST_REJECT_FIXED")) {
        errno=EINVAL; return MAP_FAILED;
    }
    return real_map(address,length,prot,flags,fd,offset);
}

static void init(void) {
    if (initialized) return;
    initialized = 1;
    unsigned w = getenv("M8_TEST_WIDTH") ? (unsigned)atoi(getenv("M8_TEST_WIDTH")) : 8;
    unsigned h = getenv("M8_TEST_HEIGHT") ? (unsigned)atoi(getenv("M8_TEST_HEIGHT")) : 4;
    current = (struct fb_var_screeninfo){.xres=w,.yres=h,.xres_virtual=w,.yres_virtual=h*2,
        .bits_per_pixel=32,.red={16,8,0},.green={8,8,0},.blue={0,8,0},.transp={24,8,0}};
    if (getenv("M8_TEST_NO_ALPHA")) current.transp=(struct fb_bitfield){0,0,0};
    fixed = (struct fb_fix_screeninfo){.id="hcfb",.smem_len=w*h*8,.line_length=w*4};
}

int open(const char *path, int flags, ...) {
    static int (*real_open)(const char *, int, ...);
    if (!real_open) real_open=dlsym(RTLD_NEXT,"open");
    mode_t mode=0;
    if (flags & O_CREAT) { va_list ap; va_start(ap,flags); mode=va_arg(ap,int); va_end(ap); }
    unsigned kind=0;
    if (!strcmp(path,"/dev/fb0")) { path=getenv("M8_TEST_FB"); kind=1; }
    else if (!strcmp(path,"/dev/dis")) { path=getenv("M8_TEST_FB"); kind=2; }
    else if (!strcmp(path,M8_BOOT_STATE)) path=getenv("M8_TEST_STATE");
    if (!path) { errno=ENOENT; return -1; }
    int fd=real_open(path,flags,mode);
    if (fd >= 0 && fd < (int)sizeof(descriptors)) descriptors[fd]=(unsigned char)kind;
    return fd;
}

int fstat(int fd, struct stat *s) {
    static int (*real_stat)(int,struct stat *);
    if (!real_stat) real_stat=dlsym(RTLD_NEXT,"fstat");
    int result=real_stat(fd,s);
    if (!result && fd >= 0 && fd < (int)sizeof(descriptors) && descriptors[fd]) {
        s->st_mode=(s->st_mode & 0777)|S_IFCHR;
        s->st_rdev=makedev(29,descriptors[fd]-1);
    }
    return result;
}

int unlink(const char *path) {
    static int (*real_unlink)(const char *);
    if (!real_unlink) real_unlink=dlsym(RTLD_NEXT,"unlink");
    if (!strcmp(path,M8_BOOT_STATE)) path=getenv("M8_TEST_STATE");
    return real_unlink(path);
}

int access(const char *path, int mode) {
    static int (*real_access)(const char *,int);
    if (!real_access) real_access=dlsym(RTLD_NEXT,"access");
    if (!strcmp(path,M8_BOOT_STATE)) path=getenv("M8_TEST_STATE");
    return real_access(path,mode);
}

int ioctl(int fd, request_type request, ...) {
    init();
    va_list ap; va_start(ap,request); void *p=va_arg(ap,void *); va_end(ap);
    if (fd < 0 || fd >= (int)sizeof(descriptors) || !descriptors[fd]) { errno=ENOTTY; return -1; }
    if (descriptors[fd] == 2 && (uint32_t)request == 0xc00c0e0cU) {
        *(struct m8_boot_screen *)p=(struct m8_boot_screen){.type=1,.width=1280,.height=720}; return 0;
    }
    switch ((uint32_t)request) {
        case FBIOGET_VSCREENINFO: *(struct fb_var_screeninfo *)p=current; return 0;
        case FBIOGET_FSCREENINFO: *(struct fb_fix_screeninfo *)p=fixed; return 0;
        case FBIOPUT_VSCREENINFO:
            if ((((struct fb_var_screeninfo *)p)->activate & FB_ACTIVATE_MASK) != FB_ACTIVATE_TEST) {
                current=*(struct fb_var_screeninfo *)p;
                fixed.line_length=current.xres_virtual*(current.bits_per_pixel/8U);
                mode_committed=1;
            }
            return 0;
        case FBIOPAN_DISPLAY:
            current.xoffset=((struct fb_var_screeninfo *)p)->xoffset;
            current.yoffset=((struct fb_var_screeninfo *)p)->yoffset;
            pan_committed=1; return 0;
        case FBIOBLANK:
            if ((uintptr_t)p == FB_BLANK_UNBLANK && getenv("M8_TEST_REQUIRE_MODE_AND_PAN") &&
                (!mode_committed || !pan_committed || current.transp.offset != 24 ||
                 current.transp.length != 8)) { errno=EIO; return -1; }
            return 0;
        case FBIOPUTCMAP: case M8_BOOT_CACHE: case M8_BOOT_SCALE: return 0;
        case FBIOGETCMAP: errno=ENOTSUP; return -1;
        default: errno=ENOTTY; return -1;
    }
}

void m8_test_surface(void *pixels, uint32_t value) { surface=pixels; color=value; }
void SDL_RenderPresent(void *renderer) {
    (void)renderer; init();
    if (surface) for(unsigned i=0;i<current.xres*current.yres;++i) surface[i]=color;
}
int SDL_UpdateWindowSurface(void *window) { SDL_RenderPresent(window); return 0; }
int SDL_UpdateWindowSurfaceRects(void *window, const void *rects, int count) {
    (void)rects; (void)count; return SDL_UpdateWindowSurface(window);
}
int SDL_Flip(void *surface_arg) { return SDL_UpdateWindowSurface(surface_arg); }
void SDL_UpdateRects(void *surface_arg, int count, void *rects) {
    (void)count; (void)rects; SDL_RenderPresent(surface_arg);
}
void SDL_UpdateRect(void *surface_arg, int x, int y, unsigned w, unsigned h) {
    (void)x; (void)y; (void)w; (void)h; SDL_RenderPresent(surface_arg);
}
