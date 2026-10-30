#include <stdio.h>
#include <time.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include GFX_REFERENCE

struct GfxDimensions gfx_current_dimensions={320,240,0};
bool sm64_raster_filter_enabled(void) { return false; }
typedef void (*scan_fn)(opt_pix_t *,uint16_t *,int,float *,const Vector2 *,const struct Texture *);
static scan_fn scanners[]={opt_scan_tex_rgb,opt_scan_tex_rgba,opt_scan_tex_fog,
    opt_scan_tex_fog_rgb,opt_scan_tex,opt_scan_tex_rgba_texa,opt_scan_tex_fog_rgba,opt_scan_tex_rgb_rgb};
static opt_pix_t color[256];
static uint16_t depth[256];
static struct ShaderProgram shader;
static struct Texture texture;
static uint32_t texels[64*64];
static uint32_t digest=2166136261u;
static void hash(const void *data,size_t size) {
    const uint8_t *bytes=data;for(size_t i=0;i<size;++i)digest=(digest^bytes[i])*16777619u;
}
static void reset(float *p,Vector2 *dp,unsigned hidden) {
    for(unsigned i=0;i<256;++i) { color[i]=(opt_pix_t)(i*177+31);depth[i]=hidden==0?0:hidden==1?65535:(i&1?0:65535); }
    for(unsigned i=0;i<16;++i){p[i]=180.f+i;dp[i]=(Vector2){{0.00003f*(i+1),0.f}};}
    p[2]=0.5f;p[3]=1.0f;p[4]=0.2f;p[5]=0.3f;
}
static double seconds(void) {
    struct timespec t;clock_gettime(CLOCK_PROCESS_CPUTIME_ID,&t);return t.tv_sec+t.tv_nsec*1e-9;
}
int main(int argc,char **argv) {
    unsigned repetitions=argc>1?(unsigned)strtoul(argv[1],NULL,10):2000;
    texture=(struct Texture){.w=64,.h=64,.fw=64.f,.fh=64.f,.wrap_w=63,.wrap_h=63};
    texcache=(uint8_t*)texels;cur_shader=&shader;fog_color=(Color4){{30,50,80,255}};
    for(unsigned i=0;i<64*64;++i)texels[i]=((i*29&255)<<24)|((i*17&255)<<16)|((i*7&255)<<8)|(i*13&255);
    unsigned cases=0;const int widths[]={1,7,16,19,64,192};
    for(unsigned scan=0;scan<8;++scan)for(unsigned filter=0;filter<2;++filter)
    for(unsigned wrap=0;wrap<3;++wrap)for(unsigned mode=0;mode<3;++mode)
    for(unsigned hidden=0;hidden<3;++hidden)for(unsigned test=0;test<2;++test)
    for(unsigned write=0;write<2;++write)for(unsigned width=0;width<6;++width) {
        float p[16];Vector2 dp[16];reset(p,dp,hidden);
        texture.filter=filter;texture.wrap_mode_x=texture.wrap_mode_y=wrap;
        shader.draw_flags=mode==0?0:mode==1?DRAW_BLEND:DRAW_BLEND_EDGE;
        z_test=test;z_write=write;z_offset=0;
        scanners[scan](color,depth,widths[width],p,dp,&texture);
        hash(color,sizeof(color));hash(depth,sizeof(depth));hash(p,sizeof(p));++cases;
    }
    printf("RASTER_EQ cases=%u hash=%08x pixel_bits=%zu\n",cases,digest,sizeof(opt_pix_t)*8);
    for(unsigned hidden=0;hidden<3;++hidden) {
        texture.filter=false;texture.wrap_mode_x=texture.wrap_mode_y=WRAP_REPEAT;
        shader.draw_flags=0;z_test=z_write=true;
        double start=seconds();
        for(unsigned iteration=0;iteration<repetitions;++iteration)for(unsigned scan=0;scan<8;++scan) {
            float p[16];Vector2 dp[16];reset(p,dp,hidden);
            scanners[scan](color,depth,192,p,dp,&texture);
        }
        printf("RASTER_TIME hidden=%u seconds=%.6f repetitions=%u\n",hidden,seconds()-start,repetitions);
    }
    return 0;
}
