/* SPDX-License-Identifier: MPL-2.0
 * Copyright (c) 2026 Jose Pilas.
 * Interactive software SDL2 starter using only the public SDK helpers.
 */
#include <lilsmoke/app.h>
#include <lilsmoke/sdl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    LS_App app;
    LS_Settings settings;
    LS_InputState input;
    LS_FrameClock clock;
    int focus, menu, menu_focus, confirm, dirty, saves, loaded, save_error, trace;
} Studio;
static volatile sig_atomic_t stopped;
static void stop(int signal_number) { (void)signal_number; stopped=1; }
static const uint32_t palette[8]={0x008c89,0xef5a52,0x3769cf,0xd19b19,0x7c52ad,0x329b5e,0xd64f83,0x287e9e};
static const char *names[8]={"TEAL","CORAL","BLUE","GOLD","VIOLET","GREEN","ROSE","OCEAN"};
static void state(FILE *f, const char *prefix, const Studio *s, int clean) {
    fprintf(f,"%s{\"frames\":%llu,\"events\":%llu,\"focus\":%d,\"menu\":%d,\"selection\":%d,"
              "\"color\":%d,\"tone\":%d,\"volume\":%d,\"dirty\":%d,\"saves\":%d,\"loaded\":%d,"
              "\"audio_available\":%s,\"native_keys\":%s,\"tone_queued\":%llu,\"nonzero_samples\":%llu,"
              "\"keyboard_events\":%llu,\"joystick_events\":%llu,\"mouse_events\":%llu,\"touch_events\":%llu,"
              "\"pointer_x\":%d,\"pointer_y\":%d,"
              "\"held\":%u,\"combo_held_ms\":%u,\"focused\":%s,\"exit_reason\":%u,"
              "\"frame_ms\":%u,\"work_ms\":%u,\"average_ms\":%u,\"late_frames\":%u,"
              "\"missed_frames\":%u,\"clamped_frames\":%u,\"clock_resets\":%u,"
              "\"settings_file\":\"userdata/studio.ini\",\"clean_exit\":%s}\n",
            prefix,(unsigned long long)s->app.frames,(unsigned long long)s->app.events,s->focus,s->menu,
            s->menu==2?s->confirm:s->menu_focus,s->settings.color,s->settings.tone,s->settings.volume,
            s->dirty,s->saves,s->loaded,s->app.audio_failed?"false":"true",s->app.native_keys?"true":"false",
            (unsigned long long)s->app.tone_queued,(unsigned long long)s->app.nonzero_samples,
            (unsigned long long)s->app.keyboard_events,(unsigned long long)s->app.joystick_events,
            (unsigned long long)s->app.mouse_events,(unsigned long long)s->app.touch_events,
            s->app.pointer_x,s->app.pointer_y,s->input.held,s->app.lifecycle.combo_held_ms,
            s->app.lifecycle.focused?"true":"false",s->app.lifecycle.exit_requested,
            s->clock.stats.delta_ms,s->clock.stats.work_ms,ls_frame_average_ms(&s->clock),
            s->clock.stats.late_frames,s->clock.stats.missed_frames,
            s->clock.stats.clamped_frames,s->clock.stats.clock_resets,clean?"true":"false");
    fflush(f);
}
static void trace(Studio *s, const char *prefix) { if(s->trace)state(stdout,prefix,s,0); }
static int save(Studio *s) {
    int result=ls_settings_save(&s->settings);
    s->save_error=result!=0;
    if (!result) { ++s->saves; s->dirty=0; }
    trace(s,"SDK_STUDIO_STATE ");
    return result;
}
static int inside(int x,int y,int rx,int ry,int w,int h) { return x>=rx && y>=ry && x<rx+w && y<ry+h; }
static void adjust(Studio *s,int direction) {
    if(s->focus==0)s->settings.color=(s->settings.color+direction+8)%8;
    else if(s->focus==1)s->settings.tone=!s->settings.tone;
    else if(s->focus==2) {
        s->settings.volume+=direction*5;
        if(s->settings.volume<0)s->settings.volume=0;
        if(s->settings.volume>100)s->settings.volume=100;
    } else { (void)save(s); return; }
    s->dirty=1;
}
static void menu_activate(Studio *s,int *running) {
    if(s->menu==2) {
        if(s->confirm) {
            ls_lifecycle_request_exit(&s->app.lifecycle,LS_EXIT_APPLICATION);
            *running=0;
        }
        else s->menu=1;
    } else if(s->menu_focus==0)s->menu=0;
    else if(s->menu_focus==1)(void)save(s);
    else { s->menu=2; s->confirm=0; }
}
static void action(Studio *s,LS_Action a,int *running) {
    if(a==LS_NONE)return;
    if(a==LS_QUIT) { *running=0; return; }
    if(a==LS_PAUSE) { if(!s->menu) { s->menu=1; s->menu_focus=0; } return; }
    if(a==LS_MENU) { s->menu=s->menu?0:1; s->menu_focus=0; }
    else if(a==LS_BACK) {
        if(s->menu==2)s->menu=1;
        else { s->menu=s->menu?0:1; s->menu_focus=0; }
    } else if(a==LS_POINTER) {
        int x=s->app.pointer_x,y=s->app.pointer_y;
        if(!s->menu) {
            if(inside(x,y,576,12,40,40)) { s->menu=1; s->menu_focus=0; }
            for(int i=0;i<4;++i)if(inside(x,y,324,94+i*76,292,64)) {
                s->focus=i;
                if(i==2) {
                    s->settings.volume=(x-340)*100/260;
                    if(s->settings.volume<0)s->settings.volume=0;
                    if(s->settings.volume>100)s->settings.volume=100;
                    s->dirty=1;
                } else adjust(s,1);
            }
        } else if(s->menu==1) {
            for(int i=0;i<3;++i)if(inside(x,y,190,164+i*56,260,46)) {
                s->menu_focus=i; menu_activate(s,running);
            }
        } else {
            if(inside(x,y,188,266,116,44)) { s->confirm=0; menu_activate(s,running); }
            else if(inside(x,y,336,266,116,44)) { s->confirm=1; menu_activate(s,running); }
        }
    } else if(s->menu) {
        if(s->menu==2 && (a==LS_LEFT||a==LS_RIGHT||a==LS_UP||a==LS_DOWN))s->confirm=!s->confirm;
        else if(s->menu==1 && a==LS_UP)s->menu_focus=(s->menu_focus+2)%3;
        else if(s->menu==1 && a==LS_DOWN)s->menu_focus=(s->menu_focus+1)%3;
        else if(a==LS_ACTIVATE)menu_activate(s,running);
    } else {
        if(a==LS_UP)s->focus=(s->focus+3)%4;
        else if(a==LS_DOWN)s->focus=(s->focus+1)%4;
        else if(a==LS_LEFT)adjust(s,-1);
        else if(a==LS_RIGHT||a==LS_ACTIVATE)adjust(s,1);
    }
    trace(s,"SDK_STUDIO_STATE ");
}
static void draw(Studio *s) {
    LS_App *a=&s->app;
    char buffer[64];
    uint32_t accent=palette[s->settings.color];
    ls_begin(a,0xf1f4f4);
    ls_fill(a,0,0,640,64,0xffffff);
    ls_text(a,"SDK STUDIO",24,20,3,0x20282b);
    ls_fill(a,576,12,40,40,0xe4eaea);
    ls_fill(a,589,23,4,18,0x354446); ls_fill(a,599,23,4,18,0x354446);
    ls_text(a,s->menu?"PAUSED":"SIGNAL",24,88,2,0x58686a);
    /* Software-only procedural artwork reflects the actual editable state. */
    int pulse=s->settings.tone&&!s->menu?(int)(a->frames%30):0;
    int radius=64+(pulse<15?pulse:30-pulse);
    for(int y=-radius;y<=radius;++y) {
        int x=radius;
        while(x*x+y*y>radius*radius)--x;
        ls_line(a,154-x,216+y,154+x,216+y,accent);
    }
    ls_fill(a,140,202,28,28,0xf1f4f4);
    ls_text(a,names[s->settings.color],24,334,3,0x253537);
    for(int x=24;x<282;++x) {
        int wave=s->settings.tone&&!s->menu?((x+(int)(a->frames*3))%48):24;
        wave=wave<24?wave:48-wave;
        ls_line(a,x,405-wave,x,405-wave,accent);
    }
    static const char *labels[]={"COLOR","TONE","VOLUME","SAVE"};
    for(int i=0;i<4;++i) {
        int y=94+i*76;
        ls_fill(a,324,y,292,64,(!s->menu&&s->focus==i)?0xd9efec:0xffffff);
        if(!s->menu&&s->focus==i)ls_fill(a,324,y,4,64,0x008c89);
        ls_text(a,labels[i],340,y+10,2,0x253537);
        const char *value="";
        if(i==0)value=names[s->settings.color];
        if(i==1)value=s->settings.tone?"ON - 440 HZ":"OFF";
        if(i==2) { snprintf(buffer,sizeof buffer,"%d PERCENT",s->settings.volume); value=buffer; }
        if(i==3)value=s->save_error?"SAVE FAILED":s->dirty?"CHANGED":"SAVED";
        ls_text(a,value,340,y+37,2,0x58686a);
        if(i<3)ls_text(a,">",590,y+25,2,0x008c89);
    }
    snprintf(buffer,sizeof buffer,"FRAME %u MS / WORK %u / AVG %u",s->clock.stats.delta_ms,
             s->clock.stats.work_ms,ls_frame_average_ms(&s->clock));
    ls_text(a,buffer,20,418,1,0x58686a);
    snprintf(buffer,sizeof buffer,"MISSED %u / HOLD %u",s->clock.stats.missed_frames,a->lifecycle.combo_held_ms);
    ls_text(a,buffer,360,418,1,0x58686a);
    ls_fill(a,0,440,640,40,0x20282b);
    ls_text(a,a->audio_failed?"AUDIO UNAVAILABLE":"SDL AUDIO / 44.1 KHZ",20,454,1,0xe3efee);
    ls_text(a,!a->lifecycle.focused?"FOCUS LOST":a->lifecycle.exit_requested?"EXIT REQUESTED":
            s->loaded==1?"SETTINGS LOADED":s->loaded<0?"SETTINGS RESET":"NEW SESSION",380,454,1,0xe3efee);
    if(s->menu) {
        ls_fill(a,164,106,312,260,0x20282b);
        ls_text(a,s->menu==2?"QUIT STUDIO?":"PAUSED",190,126,2,0xffffff);
        if(s->menu==1) {
            static const char *items[]={"RESUME","SAVE SETTINGS","QUIT"};
            for(int i=0;i<3;++i) {
                ls_fill(a,190,164+i*56,260,46,i==s->menu_focus?0x008c89:0x354446);
                ls_text(a,items[i],206,180+i*56,2,0xffffff);
            }
        } else {
            ls_text(a,s->save_error?"SAVE FAILED":s->dirty?"UNSAVED CHANGES":"ALL CHANGES SAVED",188,190,2,0xb9cece);
            ls_fill(a,188,266,116,44,!s->confirm?0x008c89:0x354446);
            ls_fill(a,336,266,116,44,s->confirm?0xcf514a:0x354446);
            ls_text(a,"CANCEL",198,281,2,0xffffff); ls_text(a,"QUIT",350,281,2,0xffffff);
        }
    }
}
int main(int argc,char **argv) {
    Studio s;
    const char *screenshot=NULL,*report=NULL,*audio=NULL;
    unsigned long frame_limit=0;
    memset(&s,0,sizeof s);
    s.settings.volume=35;
    const char *trace_value=getenv("LILSMOKE_SDK_TRACE");
    s.trace=trace_value&&!strcmp(trace_value,"1");
    for(int i=1;i<argc;++i) {
        if(!strcmp(argv[i],"--test-frames")&&i+1<argc) {
            char *end; frame_limit=strtoul(argv[++i],&end,10);
            if(*end||!frame_limit||frame_limit>100000)return 2;
        } else if(!strcmp(argv[i],"--test-screenshot")&&i+1<argc)screenshot=argv[++i];
        else if(!strcmp(argv[i],"--test-report")&&i+1<argc)report=argv[++i];
        else if(!strcmp(argv[i],"--test-audio")&&i+1<argc)audio=argv[++i];
        else { fprintf(stderr,"Unknown/incomplete argument: %s\n",argv[i]); return 2; }
    }
    signal(SIGINT,stop); signal(SIGTERM,stop);
    s.loaded=ls_settings_load(&s.settings);
    s.dirty=s.loaded!=1;
    const char *title=lilsmoke_app_name();
    if(ls_app_open(&s.app,title?title:"SDK Studio",audio!=NULL)) {
        fprintf(stderr,"SDK_STUDIO_ERROR %s\n",SDL_GetError()); return 3;
    }
    int running=1, ready=0;
    (void)ls_frame_init(&s.clock,33,SDL_GetTicks());
    while(running&&!stopped) {
        SDL_Event e;
        while(SDL_PollEvent(&e))action(&s,ls_input(&s.app,&e),&running);
        ls_app_poll(&s.app,&s.input);
        if(s.app.lifecycle.exit_requested)running=0;
        uint64_t before_audio=s.app.tone_queued;
        (void)ls_audio_pump(&s.app,s.settings.tone&&!s.menu,s.settings.volume);
        draw(&s);
        ls_present(&s.app);
        if(!ready) { trace(&s,"SDK_STUDIO_READY "); ready=1; }
        if((!before_audio&&s.app.tone_queued)||s.app.frames%60==0)trace(&s,"SDK_STUDIO_STATE ");
        if(frame_limit&&s.app.frames>=frame_limit)running=0;
        ls_frame_pace(&s.clock);
    }
    if(stopped)ls_lifecycle_request_exit(&s.app.lifecycle,LS_EXIT_APPLICATION);
    int failed=save(&s)!=0;
    /* SDL invalidates the backbuffer after Present; redraw for test capture. */
    if(screenshot) {
        draw(&s);
        if(ls_screenshot(&s.app,screenshot))failed=1;
    }
    if(audio&&ls_audio_wav(&s.app,audio))failed=1;
    if(report) {
        FILE *f=fopen(report,"w");
        if(!f)failed=1;
        else { state(f,"",&s,1); if(fclose(f))failed=1; }
    }
    ls_app_close(&s.app);
    if(s.trace)state(stdout,"SDK_STUDIO_EXIT ",&s,1);
    return failed?4:0;
}
