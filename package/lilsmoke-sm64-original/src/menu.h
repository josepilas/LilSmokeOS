/* SPDX-License-Identifier: MPL-2.0 */
#ifndef LILSMOKE_SM64_MENU_H
#define LILSMOKE_SM64_MENU_H
#include "world_scale.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>

enum { SM64_MENU_UP=1, SM64_MENU_DOWN=2, SM64_MENU_LEFT=4, SM64_MENU_RIGHT=8,
       SM64_MENU_A=16, SM64_MENU_B=32, SM64_MENU_START=64 };
struct sm64_menu { unsigned row,quality,smooth,volume,previous; int error; };
static const char *const sm64_menu_presets[]={"DETAIL","BALANCED","QUALITY","PERFORMANCE","ADAPTIVE"};
static const char *const sm64_menu_values[]={"detail","balanced","quality","performance","adaptive"};

static inline int sm64_menu_save(const struct sm64_menu *m,const char *home) {
    char directory[1024],file[1100],temporary[1150],data[128];
    if (!home || m->quality>4 || m->smooth>1 || m->volume>100 ||
        snprintf(directory,sizeof(directory),"%s/.sm64-port",home)>=(int)sizeof(directory) ||
        snprintf(file,sizeof(file),"%s/lilsmoke-options.conf",directory)>=(int)sizeof(file) ||
        snprintf(temporary,sizeof(temporary),"%s.new-%ld",file,(long)getpid())>=(int)sizeof(temporary)) return 0;
    struct stat st;
    if (mkdir(directory,0700) && errno!=EEXIST) return 0;
    if (lstat(directory,&st) || !S_ISDIR(st.st_mode)) return 0;
    if (!lstat(file,&st) && !S_ISREG(st.st_mode)) return 0;
    int count=snprintf(data,sizeof(data),"Quality=%s\nScale=%s\nVolume=%u\n",
        sm64_menu_values[m->quality],m->smooth?"smooth":"nearest",m->volume);
    int fd=open(temporary,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW,0600);
    if (fd<0) return 0;
    int okay=write(fd,data,(size_t)count)==count && fsync(fd)==0;
    if (close(fd)) okay=0;
    if (okay && rename(temporary,file)==0) {
        int dir=open(directory,O_RDONLY|O_DIRECTORY);
        if (dir>=0) { int result=fsync(dir);okay=result==0 || errno==EINVAL;close(dir); }
        else okay=0;
        return okay;
    }
    unlink(temporary);
    return 0;
}

/* Return 1 to resume, 2 to exit, or 3 after a setting changed. */
static inline int sm64_menu_step(struct sm64_menu *m,unsigned held,const char *home) {
    unsigned pressed=held&~m->previous;m->previous=held;
    if (pressed&(SM64_MENU_B|SM64_MENU_START)) return 1;
    if (pressed&SM64_MENU_UP) m->row=(m->row+4)%5;
    if (pressed&SM64_MENU_DOWN) m->row=(m->row+1)%5;
    if (pressed&SM64_MENU_A) { if (!m->row) return 1;if (m->row==4) return 2; }
    int direction=!!(pressed&SM64_MENU_RIGHT)-!!(pressed&SM64_MENU_LEFT);
    if ((pressed&SM64_MENU_A) && m->row<4) direction=1;
    if (!direction || !m->row || m->row==4) return 0;
    struct sm64_menu candidate=*m;
    if (m->row==1) candidate.quality=(m->quality+5+direction)%5;
    else if (m->row==2) candidate.smooth=!m->smooth;
    else {
        int volume=(int)m->volume+direction*5;
        candidate.volume=volume<0?0:volume>100?100:(unsigned)volume;
    }
    if (candidate.quality==m->quality && candidate.smooth==m->smooth && candidate.volume==m->volume) return 0;
    if (!sm64_menu_save(&candidate,home)) { m->error=1;return 0; }
    *m=candidate;m->error=0;return 3;
}

/* Project-owned 5x7 glyphs, shared in shape with the shutdown display. */
static const unsigned char sm64_menu_glyphs[36][7]={
 {14,17,17,31,17,17,17},{30,17,17,30,17,17,30},{15,16,16,16,16,16,15},
 {30,17,17,17,17,17,30},{31,16,16,30,16,16,31},{31,16,16,30,16,16,16},
 {15,16,16,23,17,17,15},{17,17,17,31,17,17,17},{31,4,4,4,4,4,31},
 {7,2,2,2,18,18,12},{17,18,20,24,20,18,17},{16,16,16,16,16,16,31},
 {17,27,21,21,17,17,17},{17,25,21,19,17,17,17},{14,17,17,17,17,17,14},
 {30,17,17,30,16,16,16},{14,17,17,17,21,18,13},{30,17,17,30,20,18,17},
 {15,16,16,14,1,1,30},{31,4,4,4,4,4,4},{17,17,17,17,17,17,14},
 {17,17,17,17,17,10,4},{17,17,17,21,21,21,10},{17,17,10,4,10,17,17},
 {17,17,10,4,4,4,4},{31,1,2,4,8,16,31},
 {14,17,19,21,25,17,14},{4,12,4,4,4,4,14},{14,17,1,2,4,8,31},
 {30,1,1,14,1,1,30},{2,6,10,18,31,2,2},{31,16,16,30,1,1,30},
 {14,16,16,30,17,17,14},{31,1,2,4,8,8,8},{14,17,17,14,17,17,14},
 {14,17,17,15,1,1,14}
};
static inline void sm64_menu_text(sm64_scale_pixel *pixels,const char *text,unsigned x,unsigned y,unsigned scale,sm64_scale_pixel color) {
    for (unsigned i=0;text[i];++i) {
        unsigned char c=(unsigned char)text[i];
        int glyph=c>='A'&&c<='Z'?c-'A':c>='0'&&c<='9'?c-'0'+26:-1;
        if (glyph<0) continue;
        for (unsigned yy=0;yy<7;++yy) for (unsigned xx=0;xx<5;++xx)
            if (sm64_menu_glyphs[glyph][yy]&(16>>xx))
                for (unsigned dy=0;dy<scale;++dy) for (unsigned dx=0;dx<scale;++dx) {
                    unsigned px=x+i*6*scale+xx*scale+dx,py=y+yy*scale+dy;
                    if (px<320 && py<240) pixels[py*320+px]=color;
                }
    }
}
static inline sm64_scale_pixel sm64_menu_color(unsigned r,unsigned g,unsigned b) {
#ifdef CONVERT
    return (sm64_scale_pixel)((r>>3)<<11|(g>>2)<<5|(b>>3));
#else
    return 0xff000000u|r<<16|g<<8|b;
#endif
}
static inline void sm64_menu_render(const struct sm64_menu *m,sm64_scale_pixel *pixels) {
    sm64_scale_pixel bg=sm64_menu_color(23,29,31),white=sm64_menu_color(240,244,242),
        focus=sm64_menu_color(36,96,83),muted=sm64_menu_color(153,171,170);
    for (unsigned i=0;i<320*240;++i) pixels[i]=bg;
    sm64_menu_text(pixels,"SM64 OPTIONS",16,16,2,white);
    const char *labels[]={"RESUME","QUALITY","SCALING","VOLUME","RETURN TO APPS"};
    for (unsigned row=0;row<5;++row) {
        unsigned y=53+row*32;
        if (row==m->row) for (unsigned yy=y-7;yy<y+21;++yy) for (unsigned x=8;x<312;++x) pixels[yy*320+x]=focus;
        sm64_menu_text(pixels,labels[row],16,y,2,white);
        if (row==1) sm64_menu_text(pixels,sm64_menu_presets[m->quality],120,y+3,1,muted);
        if (row==2) sm64_menu_text(pixels,m->smooth?"SMOOTH":"NEAREST",120,y+3,1,muted);
        if (row==3) { char volume[4];snprintf(volume,sizeof(volume),"%u",m->volume);sm64_menu_text(pixels,volume,120,y+3,1,muted); }
    }
    if (m->error) sm64_menu_text(pixels,"SETTINGS NOT SAVED",16,222,1,sm64_menu_color(244,151,145));
}
#endif
