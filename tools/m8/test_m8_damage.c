#define _POSIX_C_SOURCE 200809L
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
static unsigned char memory[16000], previous[16000];
static struct fb_var_screeninfo mode;
static int opens, closes, maps, unmaps, bad_id, bad_memory, reject_cache, reject_map;
static size_t mapped_length;
static int mock_open(const char *path, int flags, ...) {
    assert(!strcmp(path, "/dev/fb0") && (flags & O_RDWR)); ++opens; return 91;
}
static int mock_close(int fd) { assert(fd == 91); ++closes; return 0; }
static int mock_ioctl(int fd, unsigned long request, ...) {
    assert(fd == 91); va_list args; va_start(args, request);
    int result = 0;
    if (request == FBIOGET_VSCREENINFO) *va_arg(args, struct fb_var_screeninfo *) = mode;
    else if (request == FBIOGET_FSCREENINFO) {
        struct fb_fix_screeninfo *f = va_arg(args, void *); memset(f, 0, sizeof(*f));
        memcpy(f->id, bad_id ? "cirrus" : "hcfb", bad_id ? 6 : 4);
        f->line_length = 288; f->smem_len = bad_memory ? 1 : sizeof(memory);
    } else {
        assert(request == 0x20000d04UL && va_arg(args, unsigned long) == 0);
        if (reject_cache) result = -1;
    }
    va_end(args); return result;
}
static void *mock_mmap(void *p, size_t length, int prot, int flags, int fd, off_t offset) {
    assert(!p && length == ((size_t)mode.yoffset + mode.yres - 1) * 288 +
           ((size_t)mode.xoffset + mode.xres) * 4 && length <= sizeof(memory) &&
           (prot & PROT_WRITE) && flags == MAP_SHARED && fd == 91 && !offset);
    mapped_length = length;
    ++maps; return reject_map ? MAP_FAILED : memory;
}
static int mock_munmap(void *p, size_t length) { assert(p == memory && length == mapped_length); ++unmaps; return 0; }
#define open mock_open
#define close mock_close
#define ioctl mock_ioctl
#define mmap mock_mmap
#define munmap mock_munmap
#include "../../board/segam-m8-v6-stock/m8-damage.c"
#undef open
#undef close
#undef ioctl
#undef mmap
#undef munmap
int main(void) {
    unsigned char input[2048] __attribute__((aligned(16)));
    unsigned char output[2048] __attribute__((aligned(16)));
    unsigned char expected[2048];
    for (unsigned i = 0; i < sizeof(input); ++i) input[i] = (unsigned char)(i * 17U + 31U);
    for (unsigned source_offset = 0; source_offset < 4; ++source_offset)
        for (unsigned destination_offset = 0; destination_offset < 4; ++destination_offset)
            for (unsigned count = 0; count <= 257; ++count) {
                memset(output, 0x91, sizeof(output)); memcpy(expected, output, sizeof(output));
                const unsigned target = 16 + destination_offset;
                for (unsigned x = 0; x < count; ++x) {
                    uint32_t p; memcpy(&p, input + source_offset + x * 4, 4);
                    p |= UINT32_C(0xff000000); memcpy(expected + target + x * 4, &p, 4);
                }
                damage_copy_opaque(output + target, input + source_offset, count);
                assert(!memcmp(output, expected, sizeof(output)));
            }
    puts("PASS: 4128 aligned/unaligned opaque-copy cases and boundary canaries.");
    uint32_t pixels[64 * 48];
    for (unsigned i = 0; i < 64 * 48; ++i) pixels[i] = i;
    mode = (struct fb_var_screeninfo){ .xres=64, .yres=48, .xres_virtual=72, .yres_virtual=52,
        .xoffset=2, .yoffset=1, .bits_per_pixel=32,
        .red={16,8,0}, .green={8,8,0}, .blue={0,8,0}, .transp={24,8,0} };
    int32_t rect[] = {4, 6, 8, 9};
    unsetenv("LILSMOKE_HCFB");
    assert(!m8_damage_present(pixels, 64, 48, 256, rect, 1) && !opens);
    setenv("LILSMOKE_HCFB", "1", 1);
    memset(memory, 0xee, sizeof(memory)); memcpy(previous, memory, sizeof(memory));
    assert(m8_damage_present(pixels, 64, 48, 256, rect, 1) && maps == 1);
    for (unsigned y = 0; y < 52; ++y) for (unsigned x = 0; x < 72; ++x) {
        uint32_t value; memcpy(&value, memory + y * 288 + x * 4, 4);
        if (x >= 6 && x < 14 && y >= 7 && y < 16)
            assert(value == (pixels[(y-1)*64 + x-2] | 0xff000000u));
        else assert(value == 0xeeeeeeeeu);
    }
    memcpy(previous, memory, sizeof(memory));
    int32_t bad_batch[] = {0, 0, 2, 2, 63, 47, 2, 2};
    assert(!m8_damage_present(pixels, 64, 48, 256, bad_batch, 2));
    assert(!memcmp(previous, memory, sizeof(memory)));
    assert(m8_damage_present(pixels, 64, 48, 256, NULL, 0));
    assert(!m8_damage_present(pixels, 64, 48, 255, rect, 1));
    assert(!m8_damage_present(pixels, 64, 48, 256, rect, 2049));
    bad_memory = 1; assert(!m8_damage_present(pixels, 64, 48, 256, rect, 1) && unmaps == 1);
    bad_memory = 0; bad_id = 1; assert(!m8_damage_present(pixels, 64, 48, 256, rect, 1));
    bad_id = 0; reject_cache = 1; assert(!m8_damage_present(pixels, 64, 48, 256, rect, 1));
    reject_cache = 0; reject_map = 1; assert(!m8_damage_present(pixels, 64, 48, 256, rect, 1));
    reject_map = 0; mode.bits_per_pixel = 16; assert(!m8_damage_present(pixels, 64, 48, 256, rect, 1));
    mode.bits_per_pixel = 32;
    int32_t all[] = {0, 0, 64, 48};
    assert(m8_damage_present(pixels, 64, 48, 256, all, 1));
    lilsmoke_damage_reset(); assert(closes == opens);
    puts("PASS: opaque damage pixels, offsets/pitch, untouched regions, batch bounds before writes, disabled/wrong-device/mode/cache/mmap fallback and cleanup.");
    return 0;
}
