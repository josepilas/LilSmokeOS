#define _GNU_SOURCE
#include "../../board/segam-m8-v6-stock/m8-boot-logo.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void check(unsigned width, unsigned height, unsigned screen_w, unsigned screen_h,
                  unsigned source_w, unsigned source_h, unsigned bits) {
    unsigned bytes = bits / 8U;
    struct fb_var_screeninfo v = {.xres=width, .yres=height,
        .xres_virtual=width+8, .yres_virtual=height+6, .xoffset=3, .yoffset=2,
        .bits_per_pixel=bits, .red={16,8,0}, .green={8,8,0}, .blue={0,8,0}, .transp={24,8,0}};
    if (bits == 16) {
        v.red=(struct fb_bitfield){11,5,0}; v.green=(struct fb_bitfield){5,6,0};
        v.blue=(struct fb_bitfield){0,5,0}; v.transp=(struct fb_bitfield){0,0,0};
    }
    struct fb_fix_screeninfo f = {.id="hcfb", .line_length=v.xres_virtual*bytes+16U};
    f.smem_len=f.line_length*v.yres_virtual;
    unsigned char *allocation=malloc(f.smem_len+128U), *source=malloc(source_w*source_h*4U);
    unsigned char *before=malloc(f.smem_len);
    assert(allocation && source && before);
    unsigned char *map=allocation+64;
    memset(allocation,0x5a,f.smem_len+128U);
    memset(source,0xff,source_w*source_h*4U);
    assert(m8_boot_geometry(&v,&f));
    assert(m8_boot_draw(map,&v,&f,screen_w,screen_h,source,source_w,source_h));
    unsigned left=width, top=height, right=0, bottom=0, count=0;
    for (unsigned y=0;y<height;++y) for (unsigned x=0;x<width;++x) {
        uint32_t pixel=0;
        memcpy(&pixel,map+(y+v.yoffset)*(size_t)f.line_length+(x+v.xoffset)*bytes,bytes);
        if (pixel == (bits == 16 ? 0xffffU : 0xffffffffU)) {
            if (x<left) left=x;
            if (y<top) top=y;
            if (x>right) right=x;
            if (y>bottom) bottom=y;
            ++count;
        } else assert(pixel == (bits == 16 ? 0U : 0xff000000U));
    }
    assert(count && right<width && bottom<height);
    unsigned w=right-left+1U, h=bottom-top+1U, area=height;
    int progress=width>=160U && height>=120U;
    if (progress) area=height*4U/5U;
    assert(count==w*h && left==(width-w)/2U && top==(area-h)/2U);
    assert(w<=width*9U/10U && h<=area*9U/10U);
    assert(w==width*9U/10U || h==area*9U/10U);
    double ratio=(double)source_w*screen_h*width/(source_h*(double)screen_w*height);
    double error=(double)w-h*ratio;
    assert(error>=-1.001 && error<=ratio+1.001);
    if (progress) {
        assert(top+bottom<height-1U && bottom<height*80U/100U);
        assert(bottom<height*86U/100U);
        const unsigned values[]={0,5,65,100};
        for (unsigned i=0;i<sizeof(values)/sizeof(values[0]);++i) {
            memcpy(before,map,f.smem_len);
            assert(m8_boot_progress(map,&v,&f,values[i]));
            unsigned blue=0, scale=height/240U;
            if (!scale) scale=1;
            for (unsigned y=0;y<height;++y) for (unsigned x=0;x<width;++x) {
                size_t offset=(y+v.yoffset)*(size_t)f.line_length+(x+v.xoffset)*bytes;
                if (y<height*86U/100U) assert(!memcmp(map+offset,before+offset,bytes));
                if (bits==32 && map[offset]==0xed && map[offset+1]==0x97 && map[offset+2]==0x26) ++blue;
            }
            if (bits==32) assert(blue==(width*3U/5U*values[i]/100U)*(scale*4U));
        }
        assert(!m8_boot_progress(map,&v,&f,101));
    }
    for (unsigned y=0;y<v.yres_virtual;++y) for (unsigned b=0;b<f.line_length;++b) {
        if (y<v.yoffset || y>=v.yoffset+height || b<v.xoffset*bytes || b>=(v.xoffset+width)*bytes)
            assert(map[y*(size_t)f.line_length+b]==0x5a);
    }
    for (unsigned i=0;i<64;++i) assert(allocation[i]==0x5a && map[f.smem_len+i]==0x5a);
    printf("PASS layout %ux%u screen=%ux%u source=%ux%u bpp=%u bounds=%u,%u %ux%u\n",
           width,height,screen_w,screen_h,source_w,source_h,bits,left,top,w,h);
    free(before); free(source); free(allocation);
}

int main(void) {
    const unsigned sizes[][2]={{320,240},{640,480},{856,480},{1280,720},{160,120},{321,241},{20,10}};
    const unsigned sources[][2]={{1280,720},{8,4},{4,8},{4,4},{16,1},{1,16}};
    for (unsigned i=0;i<sizeof(sizes)/sizeof(sizes[0]);++i)
        for (unsigned j=0;j<sizeof(sources)/sizeof(sources[0]);++j)
            for (unsigned bits=16;bits<=32;bits+=16) {
                /* A one-pixel strip cannot always survive a tiny legacy mode. */
                if (i==6 && j>=4) continue;
                check(sizes[i][0],sizes[i][1],1280,720,sources[j][0],sources[j][1],bits);
                check(sizes[i][0],sizes[i][1],sizes[i][0],sizes[i][1],sources[j][0],sources[j][1],bits);
            }
    assert(m8_boot_remaining(100,100)==6001);
    assert(m8_boot_remaining(100,6100)==1);
    assert(!m8_boot_remaining(100,6101));
    puts("PASS upper-centered bounds, aspect, progress isolation, padded offsets, guards, six-second boundary");
    return 0;
}
