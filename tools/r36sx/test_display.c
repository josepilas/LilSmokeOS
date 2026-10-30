/* SPDX-License-Identifier: MPL-2.0 */
#define _GNU_SOURCE
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/fb.h>
#include <SDL.h>
static uint32_t screen[640*480+16];
static int opens,queries,unblanks;
static int fake_open(const char *path,int flags,...){(void)flags;assert(!strcmp(path,"/dev/fb0"));opens++;return 42;}
static int fake_close(int fd){assert(fd==42);return 0;}
static void *fake_mmap(void *a,size_t n,int p,int f,int fd,off_t o){
    (void)a;(void)p;(void)f;assert(fd==42&&o==0&&n==640*480*4);return screen;
}
static int fake_munmap(void *a,size_t n){assert(a==screen&&n==640*480*4);return 0;}
static int fake_ioctl(int fd,unsigned long req,...){
    assert(fd==42);
    va_list args;va_start(args,req);
    if(req==FBIOGET_VSCREENINFO){
        struct fb_var_screeninfo *v=va_arg(args,void *);memset(v,0,sizeof(*v));
        v->xres=640;v->yres=480;v->bits_per_pixel=32;
        v->red.offset=16;v->red.length=8;v->green.offset=8;v->green.length=8;v->blue.length=8;
        queries++;
    }else if(req==FBIOGET_FSCREENINFO){
        struct fb_fix_screeninfo *f=va_arg(args,void *);memset(f,0,sizeof(*f));
        memcpy(f->id,"hcfb",4);f->line_length=2560;f->smem_len=640*480*4;
        queries++;
    }else {assert(req==FBIOBLANK);assert(va_arg(args,int)==FB_BLANK_UNBLANK);unblanks++;}
    va_end(args);return 0;
}
#define open fake_open
#define close fake_close
#define mmap fake_mmap
#define munmap fake_munmap
#define ioctl fake_ioctl
#include "../../board/r36sx-2.7-stock/r36-display.c"
#undef open
#undef close
#undef mmap
#undef munmap
#undef ioctl
int main(void){
    assert(setenv("LILSMOKE_R36_DISPLAY","1",1)==0);
    screen[640*480]=0x12345678;
    uint16_t src[]={0xf800,0x07e0,0x001f,0xffff};
    assert(lilsmoke_present_rgb(src,2,2,4,0,4,3)==1);
    assert(screen[0]==0xffff0000&&screen[639]==0xff00ff00);
    assert(screen[479*640]==0xff0000ff&&screen[640*480-1]==0xffffffff);
    assert(screen[640*480]==0x12345678&&opens==1&&unblanks==1);
    assert(lilsmoke_present_rgb(src,2,2,4,0,13333,10000)==1);
    assert(lilsmoke_present_rgb(src,2,2,4,0,3,2)==1);
    assert(screen[0]==0xff000000&&screen[27*640]==0xffff0000);
    uint32_t *full=calloc(640*480,4);assert(full);
    full[641]=0x123456;
    int32_t rect[]={1,1,1,1};
    assert(lilsmoke_present_damage(full,640,480,2560,rect,1)==1);
    assert(screen[641]==0xff123456);
    int saved=queries;
    rect[0]=640;
    assert(lilsmoke_present_damage(full,640,480,2560,rect,1)==0&&queries==saved);
    assert(lilsmoke_present_rgb(src,641,2,1282,0,4,3)==0);
    assert(lilsmoke_present_rgb(src,2,2,4,0,0,3)==0);
    assert(lilsmoke_present_rgb(src,2,2,3,0,4,3)==0);
    assert(screen[640*480]==0x12345678);
    free(full);reset();
    puts("PASS RGB565/8888 panel scaling, aspect bars, alpha, dirty bounds, guard pixels, no /dev/dis or vendor/mode ioctl");
    return 0;
}
