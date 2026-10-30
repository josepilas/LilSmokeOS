#define _GNU_SOURCE
#include <assert.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include "../../board/segam-m8-v6-stock/m8-boot-logo.h"
#ifdef __GLIBC__
typedef unsigned long test_request;
#else
typedef int test_request;
#endif
static uint64_t now_ms, start_ms;
static int token, sleeps, interrupt_sleep, modes, pans, blanks, passed, removed, full_frames, copies, mismatch, exits, exit_status, reject_fixed;
static unsigned char physical[4096];
static struct fb_var_screeninfo current;
static struct fb_fix_screeninfo fixed;
static jmp_buf failure;
static void *shadow_address;
static void *lookup(void *, const char *);
static int stat_fd(int fd, struct stat *s) { memset(s, 0, sizeof(*s)); s->st_mode=S_IFCHR; s->st_rdev=fd == 33 ? 2 : 3; return 0; }
static int open_fb(const char *path, int flags, ...) { (void)flags; assert(!strcmp(path, "/dev/fb0")); return 33; }
static int close_fb(int fd) { assert(fd == 33); return 0; }
static int remove_token(const char *path) { assert(!strcmp(path, M8_BOOT_STATE)); ++removed; return 0; }
static int token_exists(const char *path, int mode) { assert(!strcmp(path,M8_BOOT_STATE) && mode == F_OK); return token ? 0 : -1; }
static int read_token(const char *path, uint64_t *start) { assert(!strcmp(path,M8_BOOT_STATE)); *start=start_ms; return token; }
static uint64_t clock_ms(void) { return now_ms; }
static int sleep_ms(const struct timespec *t, struct timespec *unused) {
    (void)unused; ++sleeps;
    if (interrupt_sleep) { interrupt_sleep=0; now_ms += 1000; errno=EINTR; return -1; }
    now_ms += (uint64_t)t->tv_sec*1000U + t->tv_nsec/1000000U; return 0;
}
static _Noreturn void exit_process(int status) { exit_status=status; ++exits; longjmp(failure, 1); }
#define M8_BOOT_GATE_TEST
#define dlsym lookup
#define fstat stat_fd
#define open open_fb
#define close close_fb
#define unlink remove_token
#define access token_exists
#define m8_boot_read_state read_token
#define m8_boot_now clock_ms
#define nanosleep sleep_ms
#define _exit exit_process
#define ioctl gate_ioctl
#define mmap gate_mmap
#define munmap gate_munmap
#include "../../board/segam-m8-v6-stock/m8-boot-logo-gate.c"
#undef ioctl
#undef mmap
#undef munmap

static int backend_ioctl(int fd, test_request request, ...) {
    va_list ap; va_start(ap, request); void *p=va_arg(ap,void *); va_end(ap);
    if (fd != 33) { ++passed; return 17; }
    switch ((uint32_t)request) {
        case FBIOGET_VSCREENINFO: *(struct fb_var_screeninfo *)p=current; if(mismatch) ((struct fb_var_screeninfo *)p)->xres++; break;
        case FBIOGET_FSCREENINFO: *(struct fb_fix_screeninfo *)p=fixed; break;
        case FBIOPUT_VSCREENINFO:
            if ((((struct fb_var_screeninfo *)p)->activate & FB_ACTIVATE_MASK) != FB_ACTIVATE_TEST) {
                ++modes; current=*(struct fb_var_screeninfo *)p; fixed.line_length=current.xres_virtual*(current.bits_per_pixel/8U);
            } break;
        case FBIOPAN_DISPLAY: ++pans; break;
        case FBIOBLANK: ++blanks; break;
        case M8_BOOT_CACHE: case M8_BOOT_SCALE: break;
        default: abort();
    }
    return 0;
}
static void *backend_mmap(void *address, size_t length, int prot, int flags, int fd, off_t offset) {
    (void)prot; assert(!offset && length == sizeof(physical));
    if (flags & MAP_ANONYMOUS) {
        assert(fd == -1 && (flags & MAP_FIXED) && address == shadow_address);
        return shadow_address;
    }
    assert(fd == 33);
    if (flags & MAP_FIXED) {
        assert(address == shadow_address);
        if (reject_fixed) { errno=EINVAL; return MAP_FAILED; }
        return address;
    }
    if (!shadow_address) { shadow_address=calloc(1,length); return shadow_address; }
    ++copies; return physical;
}
static int backend_munmap(void *address, size_t length) { (void)length; assert(address == physical || address == shadow_address); return 0; }
static int backend_native(const void *p, unsigned w, unsigned h, unsigned pitch, const int32_t *r, unsigned count) {
    assert(!atomic_load(&active) && count == 1 && r[0] == 0 && r[1] == 0 && r[2] == (int)w && r[3] == (int)h);
    for (unsigned y=0; y<h; ++y) memcpy(physical+y*fixed.line_length,(const char *)p+y*pitch,w*4U);
    ++full_frames; return 1;
}
static void backend_sdl(void *renderer) { (void)renderer; assert(atomic_load(&active)); memset(shadow_address,0x77,128); }
static int backend_update(void *window) { backend_sdl(window); return 0; }
static void *lookup(void *handle, const char *name) {
    assert(handle == RTLD_NEXT);
    if(!strcmp(name,"ioctl")) return (void *)backend_ioctl;
    if(!strcmp(name,"mmap")) return (void *)backend_mmap;
    if(!strcmp(name,"munmap")) return (void *)backend_munmap;
    if(!strcmp(name,"lilsmoke_present_damage")) return (void *)backend_native;
    if(!strcmp(name,"SDL_RenderPresent")) return (void *)backend_sdl;
    if(!strcmp(name,"SDL_UpdateWindowSurface")) return (void *)backend_update;
    abort();
}
static void reset(int valid, uint64_t elapsed) {
    free(shadow_address); shadow_address=NULL;
    memset(&gate,0,sizeof(gate)); gate.fd=-1; atomic_store(&active,0);
    token=valid; start_ms=10000; now_ms=start_ms+elapsed;
    sleeps=interrupt_sleep=modes=pans=blanks=passed=removed=full_frames=copies=mismatch=exits=exit_status=reject_fixed=0;
    current=(struct fb_var_screeninfo){.xres=8,.yres=4,.xres_virtual=8,.yres_virtual=8,.bits_per_pixel=32,
        .red={16,8,0},.green={8,8,0},.blue={0,8,0},.transp={24,8,0}};
    fixed=(struct fb_fix_screeninfo){.id="hcfb",.smem_len=sizeof(physical),.line_length=32};
    memset(physical,0x55,sizeof(physical));
    setenv("LILSMOKE_M8_BOOT_STATE",M8_BOOT_STATE,1); boot_gate_init();
    assert(!getenv("LILSMOKE_M8_BOOT_STATE"));
}
static void initialize_display(void) {
    struct fb_var_screeninfo v=current;
    assert(!gate_ioctl(33,FBIOPUT_VSCREENINFO,&v));
    assert(!gate_ioctl(33,FBIOPAN_DISPLAY,&v));
    assert(!gate_ioctl(33,FBIOBLANK,(void *)(uintptr_t)FB_BLANK_NORMAL));
    assert(!gate_ioctl(33,M8_BOOT_CACHE,(void *)0));
    assert(!modes && !pans && !blanks);
    void *map=gate_mmap(NULL,sizeof(physical),PROT_READ|PROT_WRITE,MAP_SHARED,33,0);
    assert(map != MAP_FAILED); memset(map,0,sizeof(physical));
    for(unsigned i=0;i<sizeof(physical);++i) assert(physical[i] == 0x55);
    assert(gate_mmap(NULL,sizeof(physical),PROT_READ|PROT_WRITE,MAP_SHARED,33,0) == MAP_FAILED);
    assert(gate_munmap(map,1) == -1);
    v.xoffset=UINT32_MAX; assert(gate_ioctl(33,FBIOPAN_DISPLAY,&v) == -1);
    v=current; v.xres_virtual=UINT32_MAX; assert(gate_ioctl(33,FBIOPUT_VSCREENINFO,&v) == -1);
    assert(gate_ioctl(44,123,(void *)0) == 17 && passed == 1);
}
int main(void) {
    unsigned char pixels[128]; memset(pixels,0x99,sizeof(pixels));
    int32_t rect[4]={2,1,2,1};
    reset(0,0); assert(!atomic_load(&active) && gate.fd == -1);
    reset(1,400); initialize_display();
    assert(!lilsmoke_present_damage(pixels,8,4,32,rect,0) && atomic_load(&active) && !sleeps);
    rect[0]=-1; assert(!lilsmoke_present_damage(pixels,8,4,32,rect,1) && atomic_load(&active)); rect[0]=2;
    interrupt_sleep=1;
    assert(lilsmoke_present_damage(pixels,8,4,32,rect,1));
    assert(now_ms == 16001 && sleeps == 2 && full_frames == 1 && removed == 1 && !atomic_load(&active));
    for(unsigned i=0;i<128;++i) assert(physical[i] == 0x99);
    assert(copies == 1 && modes == 1 && pans == 1 && blanks == 1);
    reset(1,20000); initialize_display(); SDL_RenderPresent(NULL);
    assert(!sleeps && removed == 1 && copies == 1 && !atomic_load(&active));
    for(unsigned i=0;i<128;++i) assert(physical[i] == 0x77);
    reset(1,6001); initialize_display(); assert(!SDL_UpdateWindowSurface(NULL)); assert(!sleeps && copies == 1);
    reset(1,10000); initialize_display(); mismatch=1;
    if(!setjmp(failure)) { SDL_RenderPresent(NULL); abort(); }
    assert(exits == 1 && exit_status == 70 && removed == 1); pthread_mutex_unlock(&lock);
    reset(1,10000); initialize_display(); reject_fixed=1;
    if(!setjmp(failure)) { SDL_RenderPresent(NULL); abort(); }
    assert(exits == 1 && exit_status == 70 && removed == 1); pthread_mutex_unlock(&lock);
    reset(1,100); initialize_display(); token=0;
    if(!setjmp(failure)) { SDL_RenderPresent(NULL); abort(); }
    assert(exits == 1 && exit_status == 143 && !sleeps && !modes);
    free(shadow_address);
    puts("PASS boot gate: protected setup, early/late readiness, EINTR, full-frame/SDL handoff, fd/map bounds, recovery and shutdown cancellation.");
    return 0;
}
