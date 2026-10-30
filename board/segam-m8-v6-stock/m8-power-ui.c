/* SPDX-License-Identifier: MPL-2.0 */
#define _GNU_SOURCE
#include "m8-power-ui.h"
#include "m8-native-video.h"
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <linux/fb.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

static const unsigned char letters[52][7]={
 {14,17,17,31,17,17,17},{30,17,17,30,17,17,30},{15,16,16,16,16,16,15},
 {30,17,17,17,17,17,30},{31,16,16,30,16,16,31},{31,16,16,30,16,16,16},
 {15,16,16,23,17,17,15},{17,17,17,31,17,17,17},{31,4,4,4,4,4,31},
 {7,2,2,2,18,18,12},{17,18,20,24,20,18,17},{16,16,16,16,16,16,31},
 {17,27,21,21,17,17,17},{17,25,21,19,17,17,17},{14,17,17,17,17,17,14},
 {30,17,17,30,16,16,16},{14,17,17,17,21,18,13},{30,17,17,30,20,18,17},
 {15,16,16,14,1,1,30},{31,4,4,4,4,4,4},{17,17,17,17,17,17,14},
 {17,17,17,17,17,10,4},{17,17,17,21,21,21,10},{17,17,10,4,10,17,17},
 {17,17,10,4,4,4,4},{31,1,2,4,8,16,31},
 {0,0,14,1,15,17,15},{16,16,30,17,17,17,30},{0,0,15,16,16,16,15},
 {1,1,15,17,17,17,15},{0,0,14,17,31,16,15},{6,8,28,8,8,8,8},
 {0,0,15,17,15,1,14},{16,16,30,17,17,17,17},{4,0,12,4,4,4,14},
 {2,0,6,2,2,18,12},{16,16,18,20,24,20,18},{12,4,4,4,4,4,14},
 {0,0,26,21,21,21,21},{0,0,30,17,17,17,17},{0,0,14,17,17,17,14},
 {0,0,30,17,30,16,16},{0,0,15,17,15,1,1},{0,0,22,25,16,16,16},
 {0,0,15,16,14,1,30},{8,8,28,8,8,9,6},{0,0,17,17,17,19,13},
 {0,0,17,17,17,10,4},{0,0,17,17,21,21,10},{0,0,17,10,4,10,17},
 {0,0,17,17,15,1,14},{0,0,31,2,4,8,31}
};
const char *m8_power_message(enum m8_power_screen state) {
    switch(state){
        case M8_POWER_SUCCESS:return "Power off operation succeeded";
        case M8_POWER_CHECK_CARD:return "Storage closed but SD needs repair";
        case M8_POWER_BUSY:return "Powering off safely";
        default:return "";
    }
}
static void line(uint16_t *pixels,unsigned w,unsigned h,const char *text,int y,int scale,uint16_t color) {
    int x=((int)w-(int)strlen(text)*6*scale)/2;
    for(unsigned i=0;text[i];++i){
        int glyph=text[i]>='A'&&text[i]<='Z'?text[i]-'A':text[i]>='a'&&text[i]<='z'?text[i]-'a'+26:-1;
        if(glyph<0)continue;
        for(int yy=0;yy<7;++yy)for(int xx=0;xx<5;++xx)if(letters[glyph][yy]&(16>>xx))
            for(int dy=0;dy<scale;++dy)for(int dx=0;dx<scale;++dx){
                int px=x+(int)i*6*scale+xx*scale+dx,py=y+yy*scale+dy;
                if(px>=0&&py>=0&&px<(int)w&&py<(int)h)pixels[(unsigned)py*w+(unsigned)px]=color;
            }
    }
}
void m8_power_render(uint16_t *pixels,unsigned w,unsigned h,enum m8_power_screen state) {
    if(!pixels||w<320||w>640||h<180||h>480)return;
    uint16_t background=state==M8_POWER_BLACK?0:0x10c3;
    for(size_t i=0;i<(size_t)w*h;++i)pixels[i]=background;
    if(state==M8_POWER_BLACK)return;
    const char *text=m8_power_message(state);int scale=w>=600?3:2;
    uint16_t color=state==M8_POWER_SUCCESS?0x9ef6:0xff16;
    if(strlen(text)*6U*(unsigned)scale<=w-24)line(pixels,w,h,text,((int)h-7*scale)/2,scale,color);
    else if(state==M8_POWER_SUCCESS){
        line(pixels,w,h,"Power off operation",(int)h/2-9*scale,scale,color);
        line(pixels,w,h,"succeeded",(int)h/2+2*scale,scale,color);
    } else {
        line(pixels,w,h,"Storage closed",(int)h/2-9*scale,scale,color);
        line(pixels,w,h,"SD needs repair",(int)h/2+2*scale,scale,color);
    }
}
int m8_power_show(enum m8_power_screen state) {
    static uint16_t pixels[640*360];
    m8_power_render(pixels,640,360,state);
    setenv("LILSMOKE_M8_NATIVE_VIDEO","1",1);
    if(lilsmoke_present_rgb(pixels,640,360,640*2,0,16,9))return 1;
    int fd=open("/dev/fb0",O_RDWR|O_CLOEXEC);if(fd<0)return 0;
    struct fb_fix_screeninfo f;struct fb_var_screeninfo v;
    if(ioctl(fd,FBIOGET_FSCREENINFO,&f)||ioctl(fd,FBIOGET_VSCREENINFO,&v)||
       !v.xres||!v.yres||v.xres>1920||v.yres>1080||
       (v.bits_per_pixel!=16&&v.bits_per_pixel!=24&&v.bits_per_pixel!=32)||
       !v.red.length||v.red.length>8||!v.green.length||v.green.length>8||!v.blue.length||v.blue.length>8||v.transp.length>8||
       v.red.offset+v.red.length>v.bits_per_pixel||v.green.offset+v.green.length>v.bits_per_pixel||v.blue.offset+v.blue.length>v.bits_per_pixel||
       v.transp.offset+v.transp.length>v.bits_per_pixel||f.smem_len>32U*1024*1024||
       ((uint64_t)v.yoffset+v.yres)*f.line_length>f.smem_len||
       ((uint64_t)v.xoffset+v.xres)*(v.bits_per_pixel/8)>f.line_length){close(fd);return 0;}
#ifndef M8_POWER_QA
    if(memcmp(f.id,"hcfb",4)){close(fd);return 0;}
#endif
    unsigned char *map=mmap(NULL,f.smem_len,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0);
    if(map==MAP_FAILED){close(fd);return 0;}
    unsigned bytes=v.bits_per_pixel/8;
    for(unsigned y=0;y<v.yres;++y)for(unsigned x=0;x<v.xres;++x){
        uint16_t p=pixels[(uint64_t)y*360/v.yres*640+(uint64_t)x*640/v.xres];
        unsigned red=(p>>11)*255/31,green=((p>>5)&63)*255/63,blue=(p&31)*255/31;
        uint32_t value=(red>>(8-v.red.length))<<v.red.offset|(green>>(8-v.green.length))<<v.green.offset|(blue>>(8-v.blue.length))<<v.blue.offset;
        if(v.transp.length)value|=((1U<<v.transp.length)-1)<<v.transp.offset;
        memcpy(map+(size_t)(y+v.yoffset)*f.line_length+(x+v.xoffset)*bytes,&value,bytes);
    }
    msync(map,f.smem_len,MS_SYNC);munmap(map,f.smem_len);ioctl(fd,FBIOBLANK,FB_BLANK_UNBLANK);close(fd);return 1;
}
