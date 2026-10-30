/* SPDX-License-Identifier: MPL-2.0
 * QA ONLY: fake HC device boundary, not the game or its renderer. Compiled as
 * freestanding PIC against glibc's C ABI, no musl library is linked/preloaded.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static unsigned frames,pcm_calls;
static uint64_t pcm_frames,energy;
void *g_render;
static void finish(void) {
    FILE *f=fopen("/tmp/treefrog-driver-report.txt","w");
    if(f) {
        fprintf(f,"frames=%u\npcm_calls=%u\npcm_frames=%llu\nenergy=%llu\n",frames,pcm_calls,
            (unsigned long long)pcm_frames,(unsigned long long)energy);
        fclose(f);
    }
}
static void capture(const uint16_t *pixels,int w,int h,int pitch) {
    if(w<1 || h<1 || w>1920 || h>1080 || pitch<w*2)return;
    char path[100];snprintf(path,sizeof(path),"/tmp/treefrog-frame-%04u.ppm",frames);
    FILE *f=fopen(path,"wb");if(!f)return;
    fprintf(f,"P6\n%d %d\n255\n",w,h);
    unsigned char rgb[1920*3];
    for(int y=0;y<h;++y) {
        const uint16_t *row=(const uint16_t*)((const char*)pixels+(size_t)y*pitch);
        for(int x=0;x<w;++x) {
            unsigned p=row[x];
            rgb[x*3]=(p>>11)*255/31;rgb[x*3+1]=((p>>5)&63)*255/63;rgb[x*3+2]=(p&31)*255/31;
        }
        fwrite(rgb,1,(size_t)w*3,f);
    }
    fclose(f);
}
int video_drivers_init(void) { return 1; }
void video_driver_deinit(void) { finish(); }
int video_driver_disp_frame(void *pixels,int w,int h,int pitch) {
    ++frames;
    if(frames==1)finish();
    if(frames==1 || frames==60 || frames==180)capture(pixels,w,h,pitch);
    if(frames%60==0)finish();
    return 0;
}
void fbdev_video_aspect_ratio(int fit) { (void)fit; }
int video_driver_setmode(int a,int b) { (void)a;(void)b;return 0; }
void fbdev_set_enhance(int a,int b,int c,int d,int e) { (void)a;(void)b;(void)c;(void)d;(void)e; }
int sound_driver_init(void *device,int rate,int channels) { (void)device;(void)rate;return channels==2?0:-1; }
int sound_driver_playframe(const int16_t *pcm,int count) {
    if(count<0 || count>65536 || !pcm)return -1;
    ++pcm_calls;pcm_frames+=(unsigned)count;
    for(int i=0;i<count*2;++i)energy+=(uint64_t)(pcm[i]<0?-(int)pcm[i]:pcm[i]);
    return 0;
}
int sound_driver_deinit(void) {finish();return 0;}
