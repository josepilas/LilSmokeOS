#define _GNU_SOURCE
#include "../../board/segam-m8-v6-stock/m8-boot-logo.h"
#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

int main(void) {
    assert(m8_boot_remaining(100, 100) == 6001);
    assert(m8_boot_remaining(100, 5999) == 102);
    assert(m8_boot_remaining(100, 6100) == 1);
    assert(!m8_boot_remaining(100, 6101));
    assert(!m8_boot_remaining(100, 20000));
    assert(!m8_boot_remaining(100, 99));
    assert(!m8_boot_remaining(0, 100));
    assert(!m8_boot_remaining(UINT64_MAX, UINT64_MAX));
    struct fb_var_screeninfo v = {.xres=20, .yres=10, .xres_virtual=24, .yres_virtual=12,
        .xoffset=2, .yoffset=1, .bits_per_pixel=32, .red={16,8,0}, .green={8,8,0}, .blue={0,8,0}, .transp={24,8,0}};
    struct fb_fix_screeninfo f = {.id="hcfb", .smem_len=24*12*4, .line_length=24*4};
    uint32_t stride = 0;
    assert(m8_boot_geometry(&v, &f) && m8_boot_mode(&v, f.smem_len, &stride) && stride == 96);
    unsigned char memory[24*12*4], source[4*4*4];
    memset(memory, 0x5a, sizeof(memory));
    for (unsigned i = 0; i < 16; ++i) memcpy(source + i*4, "\xff\xff\xff\xff", 4);
    assert(m8_boot_draw(memory, &v, &f, 1920, 1080, source, 4, 4));
    unsigned white = 0;
    for (unsigned y = 0; y < 12; ++y) for (unsigned x = 0; x < 24; ++x) {
        unsigned char *p = memory + (y*24+x)*4;
        if (y < 1 || y >= 11 || x < 2 || x >= 22) assert(p[0] == 0x5a && p[3] == 0x5a);
        else { assert(p[3] == 255); white += p[0] == 255; }
    }
    /* Square artwork remains square on 16:9 HDMI, not 2:1 logical pixels. */
    assert(white == 90);
    struct fb_var_screeninfo bad = v;
    bad.yoffset = UINT32_MAX; assert(!m8_boot_geometry(&bad, &f));
    bad = v; bad.yres = 0; assert(!m8_boot_geometry(&bad, &f));
    bad = v; bad.green.offset = 16; assert(!m8_boot_geometry(&bad, &f));
    bad = v; bad.red.length = 32; assert(!m8_boot_geometry(&bad, &f));
    bad = v; bad.blue.msb_right = 1; assert(!m8_boot_geometry(&bad, &f));
    bad = v; bad.xres_virtual = UINT32_MAX; assert(!m8_boot_mode(&bad, f.smem_len, &stride));
    f.line_length = 8; assert(!m8_boot_geometry(&v, &f));
    f.line_length = 96; f.smem_len = M8_BOOT_MAX_MAP + 1; assert(!m8_boot_geometry(&v, &f));
    f.smem_len = sizeof(memory); memcpy(f.id, "other", 5); assert(!m8_boot_geometry(&v, &f));
    memcpy(f.id, "hcfb", 4);
    v.bits_per_pixel=16; v.red=(struct fb_bitfield){11,5,0}; v.green=(struct fb_bitfield){5,6,0};
    v.blue=(struct fb_bitfield){0,5,0}; v.transp=(struct fb_bitfield){0,0,0};
    memset(memory, 0x5a, sizeof(memory));
    assert(m8_boot_draw(memory, &v, &f, 1280, 720, source, 4, 4));
    assert(memory[1*96+2*2] == 0 && memory[1*96+2*2+1] == 0);
    assert(memory[5*96+12*2] == 255 && memory[5*96+12*2+1] == 255);
    assert(!m8_boot_draw(memory, &v, &f, 0, 720, source, 4, 4));
    v = (struct fb_var_screeninfo){.xres=320,.yres=240,.xres_virtual=324,.yres_virtual=242,
        .xoffset=2,.yoffset=1,.bits_per_pixel=32,.red={16,8,0},.green={8,8,0},
        .blue={0,8,0},.transp={24,8,0}};
    f = (struct fb_fix_screeninfo){.id="hcfb",.smem_len=324*242*4,.line_length=324*4};
    unsigned char *progress=malloc(f.smem_len); assert(progress);
    memset(progress,0x5a,f.smem_len);
    assert(m8_boot_progress(progress,&v,&f,65));
    assert(!m8_boot_progress(progress,&v,&f,101));
    unsigned blue=0;
    for(unsigned yy=0;yy<242;++yy)for(unsigned xx=0;xx<324;++xx){
        unsigned char *pixel=progress+(yy*324+xx)*4;
        if(yy<1||yy>=241||xx<2||xx>=322)assert(pixel[0]==0x5a&&pixel[3]==0x5a);
        if(pixel[0]==0xed&&pixel[1]==0x97&&pixel[2]==0x26)++blue;
    }
    assert(blue==124*4);
    free(progress);
    char path[] = "/tmp/m8-logo-state-XXXXXX";
    int fd = mkstemp(path); assert(fd >= 0);
    assert(dprintf(fd, "M8_BOOT_LOGO_V1 %llu\n", (unsigned long long)m8_boot_now()) > 0);
    close(fd);
    uint64_t start;
    assert(m8_boot_read_state(path, &start));
    assert(!chmod(path, 0666)); assert(!m8_boot_read_state(path, &start));
    assert(!unlink(path));
    assert(!symlink("/dev/null", path)); assert(!m8_boot_read_state(path, &start)); assert(!unlink(path));
    puts("PASS boot-logo: six-second boundary, late readiness, geometry/overflow/format bounds, padded/offset ARGB and RGB565, HDMI aspect, private timing token.");
    return 0;
}
