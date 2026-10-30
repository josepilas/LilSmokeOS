#define _GNU_SOURCE
#include <SDL.h>
#include <assert.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

/* Exercise the actual gate + display + native backend, with a DirectFB
 * mapping already alive. The former test covered only the UI damage API. */
int main(int argc, char **argv) {
    assert(argc == 2);
    const int menu = !strncmp(argv[1], "menu", 4);
    const int rgb32 = strstr(argv[1], "32") != NULL;
    const int flip = !strcmp(argv[1], "flip");
    const int ungated = getenv("M8_TEST_UNGATED") != NULL;
    int fd = open("/dev/fb0", O_RDWR);
    int file = open(getenv("M8_TEST_FB"), O_RDONLY);
    assert(fd >= 0 && file >= 0);
    struct fb_var_screeninfo v;
    struct fb_fix_screeninfo f;
    assert(!ioctl(fd, FBIOGET_VSCREENINFO, &v));
    assert(!ioctl(fd, FBIOGET_FSCREENINFO, &f));
    assert(v.xres == 8 && v.yres == 4);
    void *directfb = mmap(NULL, f.smem_len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    assert(directfb != MAP_FAILED);
    uint32_t initial[64], physical[64];
    assert(pread(file, initial, sizeof(initial), 0) == sizeof(initial));
    memset(directfb, 0, f.smem_len);
    assert(!ioctl(fd, FBIOPUT_VSCREENINFO, &v));
    assert(!ioctl(fd, FBIOPAN_DISPLAY, &v));
    uint32_t pixels32[32] = {0};
    uint16_t pixels16[32] = {0};
    SDL_PixelFormat format = {.BitsPerPixel=16, .BytesPerPixel=2,
        .Rmask=0xf800, .Gmask=0x07e0, .Bmask=0x001f};
    SDL_Surface surface = {.format=&format, .w=8, .h=4, .pitch=16, .pixels=pixels16};
    int (*present)(const void *, unsigned, unsigned, unsigned, int, unsigned, unsigned) =
        dlsym(RTLD_DEFAULT, "lilsmoke_present_rgb");
    int (*present_menu)(const void *, unsigned, unsigned, unsigned, int) =
        dlsym(RTLD_DEFAULT, "lilsmoke_present_menu");
    int (*present_flip)(SDL_Surface *) = dlsym(RTLD_DEFAULT, "SDL_Flip");
    void (*reset)(void) = dlsym(RTLD_DEFAULT, "lilsmoke_present_reset");
    assert(present && present_menu && present_flip && reset);
    void *pixels = rgb32 ? (void *)pixels32 : (void *)pixels16;
    unsigned pitch = rgb32 ? 32 : 16;
    if (!ungated) {
        int result = flip ? present_flip(&surface) :
            menu ? present_menu(pixels, 8, 4, pitch, rgb32) :
            present(pixels, 8, 4, pitch, rgb32, 4, 3);
        assert(result == (flip ? 0 : 1));
        assert(!access(getenv("M8_TEST_STATE"), F_OK));
        assert(pread(file, physical, sizeof(physical), 0) == sizeof(physical));
        assert(!memcmp(initial, physical, sizeof(initial)));
    }
    for (unsigned i=0; i<32; ++i) {
        pixels32[i] = UINT32_C(0xffff0000);
        pixels16[i] = menu ? UINT16_C(0xf00f) : UINT16_C(0xf800);
    }
    for (unsigned frame=0; frame<1000; ++frame) {
        int result = flip ? present_flip(&surface) :
            menu ? present_menu(pixels, 8, 4, pitch, rgb32) :
            present(pixels, 8, 4, pitch, rgb32, 4, 3);
        assert(result == (flip ? 0 : 1));
    }
    assert(access(getenv("M8_TEST_STATE"), F_OK) != 0);
    assert(!ioctl(fd, FBIOGET_VSCREENINFO, &v));
    assert(!ioctl(fd, FBIOGET_FSCREENINFO, &f));
    assert(v.bits_per_pixel == 16 && v.xres <= 16 && v.yres == 4);
    uint16_t red;
    assert(pread(file, &red, sizeof(red), (off_t)v.yoffset * f.line_length) == sizeof(red));
    assert(red == UINT16_C(0xf800));
    reset();
    assert(!ioctl(fd, FBIOGET_VSCREENINFO, &v) && v.bits_per_pixel == 32);
    assert(!munmap(directfb, 256));
    close(file);
    close(fd);
    printf("PASS native loading %s: black retained, real RGB565 scanout, 1000 frames, mode restored\n", argv[1]);
    return 0;
}
