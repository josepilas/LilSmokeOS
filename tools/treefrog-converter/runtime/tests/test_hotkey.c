/* SPDX-License-Identifier: MPL-2.0 */
#define _GNU_SOURCE
#include <time.h>
#include <stdint.h>
static uint64_t test_millis;
static int hotkey_clock_gettime(clockid_t clock,struct timespec *ts) {
    (void)clock;ts->tv_sec=(time_t)(test_millis/1000);ts->tv_nsec=(long)(test_millis%1000)*1000000;return 0;
}
#define clock_gettime hotkey_clock_gettime
#define main lilretro_program_main
#include "../lilretro.c"
#undef main
#undef clock_gettime
#include "hotkey_cases.h"
#include "hotkey_transient_cases.h"

static void shared_cases(void) {
    struct ls_gp_state s={.magic=LS_GP_MAGIC,.abi=1,.pid=123};
    s.pads[3].connected=1;s.pads[3].instance=12;
    h.port=1;
    for(unsigned t=0;t<=80;t+=20)poll_shared_hotkeys(&s,true,t);
    s.pads[3].buttons=(1u<<LS_GP_START)|(1u<<LS_GP_BACK)|(1u<<LS_GP_X);
    for(unsigned t=100;t<600;t+=20){poll_shared_hotkeys(&s,true,t);assert(!h.stop);}
    poll_shared_hotkeys(&s,true,600);assert(h.stop);h.stop=false;
    for(unsigned t=620;t<=1300;t+=20){poll_shared_hotkeys(&s,true,t);assert(!h.stop);}
    /* Replacing a daemon or a mapped file invalidates in-progress holds. */
    for(unsigned scenario=0;scenario<5;scenario++) {
        s.pads[3].buttons=0;
        unsigned base=2000+scenario*2000;
        for(unsigned t=base;t<=base+80;t+=20)poll_shared_hotkeys(&s,true,t);
        s.pads[3].buttons=(1u<<LS_GP_START)|(1u<<LS_GP_BACK);
        for(unsigned t=base+100;t<base+500;t+=20)poll_shared_hotkeys(&s,true,t);
        if(scenario==0)s.pid++;
        if(scenario==1)h.gamepad.generation++;
        if(scenario==2)s.pads[3].instance++;
        if(scenario==3)s.pads[3].connected=0;
        poll_shared_hotkeys(&s,scenario!=4,base+500);
        s.pads[3].connected=1;
        for(unsigned t=base+520;t<base+1300;t+=20){poll_shared_hotkeys(&s,true,t);assert(!h.stop);}
    }
    memset(h.hotkeys,0,sizeof h.hotkeys);
    s.pads[0].connected=1;s.pads[0].buttons=0;s.pads[3].buttons=0;
    for(unsigned t=13000;t<=13080;t+=20)poll_shared_hotkeys(&s,true,t);
    s.pads[0].buttons=1u<<LS_GP_START;s.pads[3].buttons=1u<<LS_GP_BACK;
    for(unsigned t=13100;t<=14000;t+=20){poll_shared_hotkeys(&s,true,t);assert(!h.stop);}
    poll_shared_hotkeys(&s,false,14020);
}

#if SDL_MAJOR_VERSION >= 2 && SDL_VERSION_ATLEAST(2,0,14)
static void sdl_tick(uint64_t now) {test_millis=now;poll_input();}
static void set_pair(SDL_Joystick *pad,int select,int start,int down) {
    assert(SDL_JoystickSetVirtualButton(pad,select,(Uint8)down)==0);
    assert(SDL_JoystickSetVirtualButton(pad,start,(Uint8)down)==0);
}
static void virtual_cases(void) {
    assert(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_GAMECONTROLLER)==0);h.sdl=true;
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS,"1");
    int raw=SDL_JoystickAttachVirtual(SDL_JOYSTICK_TYPE_UNKNOWN,4,16,0);assert(raw==0);
    int mapped=SDL_JoystickAttachVirtual(SDL_JOYSTICK_TYPE_GAMECONTROLLER,4,16,0);assert(mapped==1);
    char guid[64],mapping[512];SDL_JoystickGetGUIDString(SDL_JoystickGetDeviceGUID(mapped),guid,sizeof guid);
    snprintf(mapping,sizeof mapping,"%s,Hotkey mapped test,a:b0,b:b1,x:b2,y:b3,back:b4,start:b6,leftx:a0,lefty:a1,rightx:a2,righty:a3,",guid);
    assert(SDL_GameControllerAddMapping(mapping)>=0);assert(SDL_IsGameController(mapped));
    SDL_Joystick *first=SDL_JoystickOpen(raw),*second=SDL_JoystickOpen(mapped);assert(first && second);
    for(unsigned t=15000;t<=15080;t+=20)sdl_tick(t);
    /* Retain legacy raw gameplay bitmasks and analog axes on configured port. */
    assert(SDL_JoystickSetVirtualAxis(first,0,12345)==0);
    assert(SDL_JoystickSetVirtualButton(first,8,1)==0);
    sdl_tick(15100);assert(input_state(h.port,RETRO_DEVICE_JOYPAD,0,8)==1);
    assert(input_state(h.port,RETRO_DEVICE_ANALOG,0,0)==12345);
    /* X/Y on a mapped controller are not physical Back/Start. */
    set_pair(second,2,3,1);
    for(unsigned t=15120;t<=15800;t+=20){sdl_tick(t);assert(!h.stop);}
    set_pair(second,2,3,0);set_pair(second,4,6,1);
    for(unsigned t=15820;t<16320;t+=20){sdl_tick(t);assert(!h.stop);}
    sdl_tick(16320);assert(h.stop);h.stop=false;
    for(unsigned t=16340;t<=17000;t+=20){sdl_tick(t);assert(!h.stop);}
    /* Held-at-hotplug stays unarmed even when device count is unchanged. */
    SDL_JoystickClose(second);assert(SDL_JoystickDetachVirtual(mapped)==0);sdl_tick(17020);
    mapped=SDL_JoystickAttachVirtual(SDL_JOYSTICK_TYPE_GAMECONTROLLER,4,16,0);assert(mapped==1);
    second=SDL_JoystickOpen(mapped);assert(second);set_pair(second,4,6,1);
    for(unsigned t=17040;t<=17800;t+=20){sdl_tick(t);assert(!h.stop);}
    set_pair(second,4,6,0);
    for(unsigned t=17820;t<=17900;t+=20)sdl_tick(t);
    /* Two devices cannot provide half a combo each. */
    assert(SDL_JoystickSetVirtualButton(first,2,1)==0);
    assert(SDL_JoystickSetVirtualButton(second,6,1)==0);
    for(unsigned t=17920;t<=18600;t+=20){sdl_tick(t);assert(!h.stop);}
    assert(SDL_JoystickSetVirtualButton(first,3,1)==0);
    for(unsigned t=18620;t<19120;t+=20){sdl_tick(t);assert(!h.stop);}
    sdl_tick(19120);assert(h.stop);h.stop=false;
    set_pair(first,2,3,0);set_pair(second,4,6,0);
    for(unsigned t=19140;t<=19220;t+=20)sdl_tick(t);
    set_pair(first,2,3,1);
    for(unsigned t=19240;t<=19640;t+=20){sdl_tick(t);assert(!h.stop);}
    SDL_JoystickClose(first);assert(SDL_JoystickDetachVirtual(raw)==0);sdl_tick(19660);
    for(unsigned t=19680;t<=20300;t+=20){sdl_tick(t);assert(!h.stop);}
    SDL_JoystickClose(second);assert(SDL_JoystickDetachVirtual(0)==0);
    for(unsigned p=0;p<HOTKEY_SDL_PADS;p++)close_sdl_exit_pad(&h.sdl_hotkeys[p]);
    if(h.joystick){SDL_JoystickClose(h.joystick);h.joystick=NULL;}
    SDL_Quit();h.sdl=false;
}
#endif
int main(void) {
    h.gamepad.fd=-1;assert(setenv("LILSMOKE_GAMEPAD_STATE","/nonexistent/lilretro-hotkey-test",1)==0);
    hotkey_cases();shared_cases();transient_cases();
#if SDL_MAJOR_VERSION >= 2 && SDL_VERSION_ATLEAST(2,0,14)
    virtual_cases();
    puts("LILRETRO_HOTKEY state/compound/slot/release/stale/replacement/SDL-mapped/SDL-raw/hotplug/normal-controls passed");
#else
    puts("LILRETRO_HOTKEY state/shared passed; SDL virtual-device tests unavailable in this SDL version");
#endif
    ls_gp_close(&h.gamepad);return 0;
}
