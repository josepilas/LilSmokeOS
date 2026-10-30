/* SPDX-License-Identifier: GPL-2.0-or-later
 * Compiled into the upstream engines, not an OS launcher or input mapper.
 * txt_font.h is GPL font data from PrBoom's embedded TEXTSCREEN library. */
#ifndef LILSMOKE_NATIVE_MENU_H
#define LILSMOKE_NATIVE_MENU_H
#include <SDL.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
typedef struct { unsigned char *data; int width,height; } txt_font_t;
#include "txt_font.h"
typedef struct { char name[256]; int directory; } lilos_entry;
static int lilos_join(char *out,size_t size,const char *base,const char *name)
{
    int n=snprintf(out,size,"%s/%s",base,name);
    return n>=0&&(size_t)n<size;
}
static int lilos_is_data(const char *path)
{
    char file[1024];struct stat st;
    if(lilos_join(file,sizeof(file),path,"npc.tbl")&&stat(file,&st)==0&&S_ISREG(st.st_mode))return 1;
    return lilos_join(file,sizeof(file),path,"data/npc.tbl")&&stat(file,&st)==0&&S_ISREG(st.st_mode);
}
static int lilos_compare(const void *a,const void *b)
{
    const lilos_entry *x=(const lilos_entry *)a,*y=(const lilos_entry *)b;
    if(x->directory!=y->directory)return y->directory-x->directory;
    return strcasecmp(x->name,y->name);
}
static unsigned lilos_scan(const char *path,int nx,lilos_entry *items)
{
    DIR *dir=opendir(path);struct dirent *entry;unsigned count=0;
    if(!dir)return 0;
    while(count<128&&(entry=readdir(dir))) {
        char file[1024];struct stat st;
        size_t len=strlen(entry->d_name);
        if(entry->d_name[0]=='.'||len>255||!lilos_join(file,sizeof(file),path,entry->d_name))continue;
        if(stat(file,&st)!=0)continue;
        int directory=S_ISDIR(st.st_mode);
        if(!directory) {
            const char *ext=strrchr(entry->d_name,'.');
            if(nx==1)continue;
            if(nx==0&&(len<4||strcasecmp(entry->d_name+len-4,".wad")))continue;
            if(nx==2&&(!ext||(strcasecmp(ext,".gba")&&strcasecmp(ext,".agb")&&strcasecmp(ext,".zip")&&
                strcasecmp(ext,".7z")&&strcasecmp(ext,".rar")&&strcasecmp(ext,".gz")&&strcasecmp(ext,".tar")&&
                strcasecmp(ext,".bz2")&&strcasecmp(ext,".xz"))))continue;
        }
        if(!directory&&!S_ISREG(st.st_mode))continue;
        snprintf(items[count].name,sizeof(items[count].name),"%s",entry->d_name);
        items[count++].directory=directory;
    }
    closedir(dir);qsort(items,count,sizeof(*items),lilos_compare);return count;
}
static void lilos_text(SDL_Surface *screen,int x,int y,const char *text,Uint32 color)
{
    for(int n=0;text[n]&&x+8<=screen->w;++n,x+=8) {
        unsigned char ch=(unsigned char)text[n];
        for(int row=0;row<16&&y+row<screen->h;++row)
            for(int col=0;col<8;++col)
                if(main_font.data[ch*16+row]&(0x80>>col)) {
                    SDL_Rect pixel={x+col,y+row,1,1};SDL_FillRect(screen,&pixel,color);
                }
    }
}
static int lilos_pick(const char *title,const char *root,int nx,char *out,size_t size)
{
    char path[1024];lilos_entry items[128];unsigned count,selected=0;
    SDL_Window *window=NULL;SDL_Surface *screen=NULL;int result=0,dirty=1,done=0;
    if(!root||strlen(root)>=sizeof(path)||!size)return -1;
    strcpy(path,root);
    setenv("LILSMOKE_NATIVE_PICKER","1",1);
    if(SDL_Init(SDL_INIT_VIDEO)<0)return -1;
    window=SDL_CreateWindow(title,SDL_WINDOWPOS_UNDEFINED,SDL_WINDOWPOS_UNDEFINED,320,240,0);
    if(!window||(screen=SDL_GetWindowSurface(window))==NULL){result=-1;goto finish;}
    count=lilos_scan(path,nx,items);
    fprintf(stderr,"OD_NATIVE_MENU engine=%s no_content=1 entries=%u\n",title,count);
    while(!done) {
        int data=nx==1&&lilos_is_data(path);
        unsigned total=count+(unsigned)data;
        if(dirty) {
            Uint32 bg=SDL_MapRGB(screen->format,19,23,27),fg=SDL_MapRGB(screen->format,236,237,240);
            Uint32 accent=SDL_MapRGB(screen->format,120,209,151);
            SDL_FillRect(screen,NULL,bg);lilos_text(screen,8,4,title,accent);
            size_t len=strlen(path);lilos_text(screen,8,24,path+(len>38?len-38:0),fg);
            if(!total)lilos_text(screen,8,64,"No content",fg);
            unsigned first=selected>=8?selected-7:0;
            for(unsigned row=0,index=first;index<total&&row<8;++index,++row) {
                const char *name=data&&index==0?"Use this data directory":items[index-(unsigned)data].name;
                if(index==selected){SDL_Rect rect={4,48+(int)row*20,312,20};SDL_FillRect(screen,&rect,SDL_MapRGB(screen->format,42,59,49));}
                lilos_text(screen,8,50+(int)row*20,index==selected?">":" ",accent);
                lilos_text(screen,24,50+(int)row*20,name,fg);
            }
            lilos_text(screen,8,216,"Back",fg);
            if(SDL_UpdateWindowSurface(window)<0){result=-1;break;}
            dirty=0;
        }
        SDL_Event event;
        if(!SDL_WaitEventTimeout(&event,50))continue;
        if(event.type==SDL_QUIT){done=1;continue;}
        if(event.type!=SDL_KEYDOWN)continue;
        SDL_Keycode key=event.key.keysym.sym;
        if(key==SDLK_DOWN&&total){selected=(selected+1)%total;dirty=1;}
        else if(key==SDLK_UP&&total){selected=(selected+total-1)%total;dirty=1;}
        else if(key==SDLK_ESCAPE||key==SDLK_x||key==SDLK_SPACE) {
            if(!strcmp(path,root)){done=1;continue;}
            char *slash=strrchr(path,'/');if(!slash||slash<path+strlen(root)){done=1;continue;}
            *slash=0;selected=0;count=lilos_scan(path,nx,items);dirty=1;
        } else if(total&&(key==SDLK_RETURN||key==SDLK_z||key==SDLK_LCTRL)) {
            if(data&&selected==0){snprintf(out,size,"%s",path);result=strlen(path)<size?1:-1;done=1;}
            else {
                lilos_entry *item=&items[selected-(unsigned)data];char chosen[1024];
                if(!lilos_join(chosen,sizeof(chosen),path,item->name))continue;
                if(item->directory){strcpy(path,chosen);selected=0;count=lilos_scan(path,nx,items);dirty=1;}
                else {snprintf(out,size,"%s",chosen);result=strlen(chosen)<size?1:-1;done=1;}
            }
        }
    }
finish:
    if(result<0)fprintf(stderr,"OD_NATIVE_MENU failed: %s\n",SDL_GetError());
    if(window)SDL_DestroyWindow(window);
    /* Keep DirectFB's input session alive when handing data to the engine. */
    if(result<=0)SDL_Quit();
    unsetenv("LILSMOKE_NATIVE_PICKER");return result;
}
#endif
