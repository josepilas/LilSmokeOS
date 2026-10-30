/* SPDX-License-Identifier: MPL-2.0 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <SDL.h>

static int fd=-1, prepared;
static unsigned char *pixels;
static size_t span;
static unsigned last_left,last_top,last_width,last_height;
static void reset(void){if(pixels)munmap(pixels,span);if(fd>=0)close(fd);pixels=NULL;span=0;fd=-1;prepared=0;last_width=last_height=0;}
/* This adapter NEVER opens /dev/dis or changes HDMI/MIPI timing, clocks,
 * pixel format or framebuffer geometry. DirectFB owns mode selection. */
static int geometry(struct fb_var_screeninfo *v,struct fb_fix_screeninfo *f){
    const char *on=getenv("LILSMOKE_R36_DISPLAY");
    if(!on||strcmp(on,"1"))return 0;
    if(fd<0)fd=open("/dev/fb0",O_RDWR|O_CLOEXEC);
    if(fd<0||ioctl(fd,FBIOGET_VSCREENINFO,v)||ioctl(fd,FBIOGET_FSCREENINFO,f))return 0;
    if(memcmp(f->id,"hcfb",4)||v->xres!=640||v->yres!=480||v->bits_per_pixel!=32||
       v->red.offset!=16||v->red.length!=8||v->green.offset!=8||v->green.length!=8||
       v->blue.offset||v->blue.length!=8||v->xoffset||v->yoffset||f->line_length<2560||
       f->line_length>16384||f->smem_len<480U*f->line_length||f->smem_len>32U*1024U*1024U)return 0;
    return 1;
}
void lilsmoke_present_checkpoint(void){
    struct fb_var_screeninfo v;struct fb_fix_screeninfo f;
    if(!prepared&&geometry(&v,&f)){(void)ioctl(fd,FBIOBLANK,FB_BLANK_UNBLANK);prepared=1;}
}
static int map_frame(struct fb_fix_screeninfo *fixed){
    struct fb_var_screeninfo v;
    if(!geometry(&v,fixed))return 0;
    size_t needed=(size_t)fixed->line_length*480;
    if(pixels&&span!=needed){reset();if(!geometry(&v,fixed))return 0;}
    if(!pixels){
        void *p=mmap(NULL,needed,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0);
        if(p==MAP_FAILED)return 0;
        pixels=p;span=needed;
    }
    return 1;
}
/* Keep the panel's native mode. Bounded software scaling replaces the M8
 * HDMI scaler ABI, including calls made directly by RetroArch and SDL1 apps. */
int lilsmoke_present_rgb(const void *source,unsigned w,unsigned h,unsigned pitch,int rgb32,unsigned num,unsigned den){
    if(!source||!w||!h||w>640||h>480||pitch<w*(rgb32?4U:2U)||pitch>16384||!num||!den||num>1000000||den>1000000)return 0;
    struct fb_fix_screeninfo fixed;
    if(!map_frame(&fixed))return 0;
    unsigned dw=640,dh=(unsigned)((uint64_t)640*den/num);
    if(dh>480){dh=480;dw=(unsigned)((uint64_t)480*num/den);}
    if(!dw||!dh)return 0;
    unsigned left=(640-dw)/2,top=(480-dh)/2;
    if(last_left!=left||last_top!=top||last_width!=dw||last_height!=dh){
        for(unsigned y=0;y<480;y++){
            uint32_t *row=(uint32_t *)(pixels+(size_t)y*fixed.line_length);
            for(unsigned x=0;x<640;x++)row[x]=0xff000000U;
        }
        last_left=left;last_top=top;last_width=dw;last_height=dh;
    }
    unsigned xs[640];for(unsigned x=0;x<dw;x++)xs[x]=(unsigned)((uint64_t)x*w/dw);
    uint32_t row[640];unsigned previous=~0U;
    for(unsigned y=0;y<dh;y++){
        unsigned sy=(unsigned)((uint64_t)y*h/dh);
        if(sy!=previous){
            const unsigned char *src=(const unsigned char *)source+(size_t)sy*pitch;
            for(unsigned x=0;x<dw;x++){
                uint32_t value;
                if(rgb32){memcpy(&value,src+xs[x]*4,4);value|=0xff000000U;}
                else {uint16_t v;memcpy(&v,src+xs[x]*2,2);
                    unsigned r=(v>>11)&31,g=(v>>5)&63,b=v&31;
                    value=0xff000000U|((r<<3|r>>2)<<16)|((g<<2|g>>4)<<8)|(b<<3|b>>2);}
                row[x]=value;
            }
            previous=sy;
        }
        memcpy(pixels+(size_t)(top+y)*fixed.line_length+left*4,row,dw*4);
    }
    lilsmoke_present_checkpoint();return 1;
}
int lilsmoke_present_menu(const void *source,unsigned w,unsigned h,unsigned pitch,int rgb32){
    return lilsmoke_present_rgb(source,w,h,pitch,rgb32,w,h);
}
void lilsmoke_present_reset(void){reset();}
static int surface_present(SDL_Surface *s){
    if(!s||!s->format||!s->pixels||s->w<1||s->h<1)return 0;
    SDL_PixelFormat *f=s->format;
    int rgb32=f->BitsPerPixel==32&&f->BytesPerPixel==4&&f->Rmask==0xff0000&&f->Gmask==0xff00&&f->Bmask==0xff;
    int rgb16=f->BitsPerPixel==16&&f->BytesPerPixel==2&&f->Rmask==0xf800&&f->Gmask==0x7e0&&f->Bmask==0x1f;
    if(!rgb32&&!rgb16)return 0;
    unsigned num=4,den=3;
    const char *engine=getenv("LILSMOKE_OD_ENGINE");
    if(engine&&(!strcmp(engine,"gpsp")||!strcmp(engine,"regba")||!strcmp(engine,"vbam"))){num=3;den=2;}
    return lilsmoke_present_rgb(s->pixels,(unsigned)s->w,(unsigned)s->h,s->pitch,rgb32,num,den);
}
int lilsmoke_present_damage(const void *source,unsigned w,unsigned h,unsigned pitch,const int32_t *rects,unsigned count){
    struct fb_fix_screeninfo f;
    if(!source||w!=640||h!=480||pitch<w*4||pitch>16384||count>2048||(count&&!rects))return 0;
    for(unsigned i=0;i<count;i++){
        const int32_t *r=rects+4*i;
        if(r[0]<0||r[1]<0||r[2]<=0||r[3]<=0||(uint64_t)r[0]+r[2]>w||(uint64_t)r[1]+r[3]>h)return 0;
    }
    if(!map_frame(&f))return 0;
    last_width=last_height=0;
    for(unsigned i=0;i<count;i++){
        const int32_t *r=rects+4*i;
        for(int y=r[1];y<r[1]+r[3];y++){
            const unsigned char *s=(const unsigned char *)source+(size_t)y*pitch+(size_t)r[0]*4;
            unsigned char *d=pixels+(size_t)y*f.line_length+(size_t)r[0]*4;
            for(int x=0;x<r[2];x++){uint32_t p;memcpy(&p,s+x*4,4);p|=0xff000000U;memcpy(d+x*4,&p,4);}
        }
    }
    lilsmoke_present_checkpoint();return 1;
}
void SDL_RenderPresent(void *r){static void(*real)(void *);if(!real)real=dlsym(RTLD_NEXT,"SDL_RenderPresent");if(real)real(r);lilsmoke_present_checkpoint();}
int SDL_UpdateWindowSurface(void *w){static int(*real)(void *);if(!real)real=dlsym(RTLD_NEXT,"SDL_UpdateWindowSurface");int rc=real?real(w):-1;lilsmoke_present_checkpoint();return rc;}
SDL_Surface *SDL_SetVideoMode(int w,int h,int depth,uint32_t flags){
    static SDL_Surface *(*real)(int,int,int,uint32_t);if(!real)real=dlsym(RTLD_NEXT,"SDL_SetVideoMode");reset();return real?real(w,h,depth,flags):NULL;
}
int SDL_Flip(SDL_Surface *s){static int(*real)(SDL_Surface *);if(!real)real=dlsym(RTLD_NEXT,"SDL_Flip");int rc=surface_present(s)?0:(real?real(s):-1);lilsmoke_present_checkpoint();return rc;}
void SDL_UpdateRects(SDL_Surface *s,int count,SDL_Rect *rects){
    static void(*real)(SDL_Surface *,int,SDL_Rect *);if(!real)real=dlsym(RTLD_NEXT,"SDL_UpdateRects");
    if(!surface_present(s)&&real)real(s,count,rects);
    lilsmoke_present_checkpoint();
}
void SDL_UpdateRect(SDL_Surface *s,int32_t x,int32_t y,uint32_t w,uint32_t h){
    static void(*real)(SDL_Surface *,int32_t,int32_t,uint32_t,uint32_t);if(!real)real=dlsym(RTLD_NEXT,"SDL_UpdateRect");
    if(!surface_present(s)&&real)real(s,x,y,w,h);
    lilsmoke_present_checkpoint();
}
void SDL_Quit(void){static void(*real)(void);if(!real)real=dlsym(RTLD_NEXT,"SDL_Quit");reset();if(real)real();}
