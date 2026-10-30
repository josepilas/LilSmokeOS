/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef LILSMOKE_MGBA_PAUSE_H
#define LILSMOKE_MGBA_PAUSE_H
#include "native-menu.h"
static void lilos_mgba_pause(struct mCoreThread *context, SDL_Window *window)
{
    const char *options[] = {"Resume", "Save state", "Load state", "Reset", "Exit game"};
    SDL_Renderer *renderer = SDL_GetRenderer(window);
    if (!renderer) return;
    SDL_Surface *screen = SDL_CreateRGBSurfaceWithFormat(0,320,240,32,SDL_PIXELFORMAT_ARGB8888);
    if (!screen) return;
    int old_w=0,old_h=0,logical_w=0,logical_h=0;
    SDL_GetRendererOutputSize(renderer,&old_w,&old_h);
    SDL_RenderGetLogicalSize(renderer,&logical_w,&logical_h);
    SDL_RenderSetLogicalSize(renderer,320,240);
    mCoreThreadPause(context);
    fprintf(stderr,"OD_NATIVE_MENU engine=mGBA pause=1\n");
    setenv("LILSMOKE_NATIVE_PICKER","1",1);
    unsigned selected=0; int dirty=1,done=0,exit_game=0;
    const char *message="";
    while (!done) {
        if (dirty) {
            Uint32 fg=SDL_MapRGB(screen->format,236,237,240);
            Uint32 accent=SDL_MapRGB(screen->format,120,209,151);
            SDL_FillRect(screen,NULL,SDL_MapRGB(screen->format,19,23,27));
            lilos_text(screen,12,12,"mGBA",accent);
            for (unsigned i=0;i<5;++i) {
                if(i==selected) { SDL_Rect r={8,48+(int)i*26,304,24}; SDL_FillRect(screen,&r,SDL_MapRGB(screen->format,42,59,49)); }
                lilos_text(screen,16,52+(int)i*26,options[i],i==selected?accent:fg);
            }
            lilos_text(screen,12,212,message,fg);
            SDL_Texture *texture=SDL_CreateTextureFromSurface(renderer,screen);
            if(!texture) break;
            SDL_RenderClear(renderer); SDL_RenderCopy(renderer,texture,NULL,NULL);
            SDL_RenderPresent(renderer); SDL_DestroyTexture(texture); dirty=0;
        }
        SDL_Event event;
        if(!SDL_WaitEventTimeout(&event,50))continue;
        if(event.type==SDL_QUIT) { done=exit_game=1; continue; }
        if(event.type!=SDL_KEYDOWN)continue;
        SDL_Keycode key=event.key.keysym.sym;
        if(key==SDLK_DOWN){selected=(selected+1)%5;dirty=1;}
        else if(key==SDLK_UP){selected=(selected+4)%5;dirty=1;}
        else if(key==SDLK_ESCAPE){done=1;}
        else if(key==SDLK_RETURN||key==SDLK_LCTRL||key==SDLK_x) {
            if(selected==0)done=1;
            else if(selected==4)done=exit_game=1;
            else if(selected==3){mCoreThreadReset(context);done=1;}
            else {
                bool ok=selected==1 ? mCoreSaveState(context->core,1,SAVESTATE_SAVEDATA|SAVESTATE_SCREENSHOT|SAVESTATE_RTC)
                                    : mCoreLoadState(context->core,1,SAVESTATE_SCREENSHOT|SAVESTATE_RTC);
                message=ok ? (selected==1?"State saved":"State loaded") : "State operation failed";
                dirty=1;
            }
        }
    }
    unsetenv("LILSMOKE_NATIVE_PICKER"); SDL_FreeSurface(screen);
    if(old_w>0&&old_h>0)SDL_RenderSetLogicalSize(renderer,old_w,old_h);
    SDL_RenderSetLogicalSize(renderer,logical_w,logical_h);
    mCoreThreadUnpause(context);
    if(exit_game)mCoreThreadEnd(context);
}
#endif
