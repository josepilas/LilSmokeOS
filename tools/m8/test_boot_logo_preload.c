#define _GNU_SOURCE
#include <assert.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
void m8_test_surface(void *, uint32_t);
void SDL_RenderPresent(void *);
int SDL_UpdateWindowSurface(void *);
int SDL_UpdateWindowSurfaceRects(void *, const void *, int);
int SDL_Flip(void *);
void SDL_UpdateRect(void *, int, int, unsigned, unsigned);
void SDL_UpdateRects(void *, int, void *);
static void handled_term(int signal_number) { (void)signal_number; }

int main(int argc, char **argv) {
    assert(argc == 2);
    if (!strcmp(argv[1], "exec")) {
        assert(getenv("LILSMOKE_M8_BOOT_STATE"));
        execl(argv[0], argv[0], "update", (char *)NULL);
        assert(0);
    }
    if (!strcmp(argv[1],"cancel")) assert(signal(SIGTERM,handled_term) != SIG_ERR);
    int fd=open("/dev/fb0",O_RDWR); assert(fd >= 0);
    struct fb_var_screeninfo v;
    struct fb_fix_screeninfo f;
    assert(!ioctl(fd,FBIOGET_VSCREENINFO,&v) && !ioctl(fd,FBIOGET_FSCREENINFO,&f));
    assert(v.xres == 8 && v.yres == 4 && f.smem_len == 256);
    int file=open(getenv("M8_TEST_FB"),O_RDONLY); assert(file >= 0);
    uint32_t initial[64], physical[64];
    assert(pread(file,initial,sizeof(initial),0) == sizeof(initial));
    void *map=mmap(NULL,f.smem_len,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0); assert(map != MAP_FAILED);
    memset(map,0,f.smem_len);
    assert(!ioctl(fd,FBIOPUT_VSCREENINFO,&v) && !ioctl(fd,FBIOPAN_DISPLAY,&v));
    assert(!ioctl(fd,FBIOBLANK,FB_BLANK_NORMAL));
    assert(pread(file,physical,sizeof(physical),0) == sizeof(physical));
    assert(!memcmp(physical,initial,sizeof(initial)));
    if (!strcmp(argv[1],"native") || !strcmp(argv[1],"alias")) {
        int (*present)(const void *,unsigned,unsigned,unsigned,const int32_t *,unsigned)=dlsym(RTLD_DEFAULT,"lilsmoke_present_damage");
        assert(present);
        uint32_t heap_frame[32];
        uint32_t *frame = !strcmp(argv[1],"alias") ? (uint32_t *)map+32 : heap_frame;
        for(unsigned i=0;i<32;++i) frame[i]=UINT32_C(0x11223344);
        const int32_t partial[4]={2,1,2,1};
        assert(present(frame,8,4,32,partial,1));
        assert(pread(file,physical,128,0) == 128);
        for(unsigned i=0;i<32;++i) assert(physical[i] == UINT32_C(0xff223344));
        if (!strcmp(argv[1],"alias")) {
            /* Rendering/commitDamage and cached DirectFB surfaces retain
             * their contents, not just their virtual addresses. */
            for(unsigned i=0;i<32;++i) assert(frame[i] == UINT32_C(0x11223344));
        }
    } else {
        if (!strcmp(argv[1], "black")) {
            m8_test_surface(map,UINT32_C(0xff000000));
            SDL_RenderPresent(NULL);
            assert(!access(getenv("M8_TEST_STATE"),F_OK));
            assert(pread(file,physical,sizeof(physical),0)==sizeof(physical));
            assert(!memcmp(physical,initial,sizeof(initial)));
        }
        m8_test_surface(map,UINT32_C(0xffaabbcc));
        if (!strcmp(argv[1],"update")) assert(!SDL_UpdateWindowSurface(NULL));
        else if (!strcmp(argv[1],"rects2")) assert(!SDL_UpdateWindowSurfaceRects(NULL, NULL, 1));
        else if (!strcmp(argv[1],"flip")) assert(!SDL_Flip(NULL));
        else if (!strcmp(argv[1],"rect")) SDL_UpdateRect(NULL,0,0,0,0);
        else if (!strcmp(argv[1],"rects")) SDL_UpdateRects(NULL,1,NULL);
        else SDL_RenderPresent(NULL);
        assert(pread(file,physical,128,0) == 128);
        for(unsigned i=0;i<32;++i) assert(physical[i] == UINT32_C(0xffaabbcc));
    }
    /* Existing DirectFB pointers must now address physical scanout, not the
     * old anonymous mapping, without rebuilding windows or textures. */
    ((uint32_t *)map)[0]=UINT32_C(0xffeeccaa);
    assert(pread(file,physical,4,0) == 4 && physical[0] == UINT32_C(0xffeeccaa));
    assert(access(getenv("M8_TEST_STATE"),F_OK) != 0);
    assert(!getenv("LILSMOKE_M8_BOOT_STATE"));
    munmap(map,f.smem_len); close(file); close(fd);
    printf("PASS actual preload chain: %s, protected setup, full frame, in-place physical remap, token consumed.\n",argv[1]);
    return 0;
}
