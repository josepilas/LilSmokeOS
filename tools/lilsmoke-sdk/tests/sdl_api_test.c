/* SPDX-License-Identifier: MPL-2.0
 * Copyright (c) 2026 Jose Pilas.
 * Host synthetic-device/API qualification; never a physical input claim.
 */
#define _POSIX_C_SOURCE 200809L
#include <lilsmoke/sdl.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static LS_Action key(LS_App *a, SDL_Keycode code) {
    SDL_Event event;
    memset(&event,0,sizeof event); event.type=SDL_KEYDOWN; event.key.keysym.sym=code;
    return ls_input(a,&event);
}
static int drain(LS_App *a,LS_Action wanted) {
    SDL_Event e; int found=0;
    SDL_JoystickUpdate();
    while(SDL_PollEvent(&e))if(ls_input(a,&e)==wanted)found=1;
    return found;
}
static void snapshot_keys(LS_App *a) {
    LS_InputState state = {0};
    SDL_Event event;
    memset(a->input_keys, 0, sizeof a->input_keys);
    memset(&event, 0, sizeof event);
    event.type = SDL_KEYDOWN; event.key.keysym.sym = SDLK_LCTRL;
    a->native_keys = 1;
    assert(ls_input(a, &event) == LS_ACTIVATE);
    ls_input_poll(a, &state);
    assert(state.held == LS_BUTTON_ACTIVATE && state.pressed == LS_BUTTON_ACTIVATE && !state.released);
    event.key.repeat = 1; (void)ls_input(a, &event);
    ls_input_poll(a, &state);
    assert(state.held == LS_BUTTON_ACTIVATE && !state.pressed && !state.released);
    event.key.repeat = 0; event.key.keysym.sym = SDLK_SPACE; (void)ls_input(a, &event);
    event.type = SDL_KEYUP; event.key.keysym.sym = SDLK_LCTRL; (void)ls_input(a, &event);
    ls_input_poll(a, &state);
    assert(state.held == LS_BUTTON_ACTIVATE && !state.released);
    event.key.keysym.sym = SDLK_SPACE; (void)ls_input(a, &event);
    ls_input_poll(a, &state);
    assert(!state.held && state.released == LS_BUTTON_ACTIVATE);
    assert(key(a, SDLK_RIGHT) == LS_RIGHT);
    ls_input_poll(a, &state);
    assert(state.pressed == LS_BUTTON_RIGHT);
    for (int i = 0; i < LS_INPUT_PADS; ++i) assert(!state.pads[i].axes_available);
    event.type = SDL_WINDOWEVENT; event.window.event = SDL_WINDOWEVENT_FOCUS_LOST;
    assert(ls_input(a, &event) == LS_PAUSE);
    ls_input_poll(a, &state);
    assert(!state.held && state.released == LS_BUTTON_RIGHT);
    event.window.event = SDL_WINDOWEVENT_FOCUS_GAINED; (void)ls_input(a, &event);
    ls_input_poll(a, &state); assert(!state.held && !state.released);
    a->native_keys = 0;
}
int main(void) {
    char root[]="/tmp/lilsmoke-sdl-api-XXXXXX",path[4096],image[4096],wav[4096];
    assert(mkdtemp(root)); setenv("LILSMOKE_APP_DIR",root,1);
    setenv("SDL_VIDEODRIVER","dummy",1); setenv("SDL_AUDIODRIVER","dummy",1);
    unsetenv("LILSMOKE_OD_NATIVE");
    LS_App a;
    assert(!ls_app_open(&a,"SDK API test",1));
    SDL_RendererInfo info;
    assert(!SDL_GetRendererInfo(a.renderer,&info) && info.flags&SDL_RENDERER_SOFTWARE);
    assert(key(&a,SDLK_UP)==LS_UP && key(&a,SDLK_DOWN)==LS_DOWN);
    assert(key(&a,SDLK_LEFT)==LS_LEFT && key(&a,SDLK_RIGHT)==LS_RIGHT);
    assert(key(&a,SDLK_LCTRL)==LS_ACTIVATE && key(&a,SDLK_SPACE)==LS_ACTIVATE);
    assert(key(&a,SDLK_LALT)==LS_BACK && key(&a,SDLK_ESCAPE)==LS_BACK);
    assert(key(&a,SDLK_RETURN)==LS_MENU);
    snapshot_keys(&a);
    SDL_Event e; memset(&e,0,sizeof e);
    e.type=SDL_MOUSEBUTTONDOWN; e.button.button=SDL_BUTTON_LEFT; e.button.x=450; e.button.y=190;
    assert(ls_input(&a,&e)==LS_POINTER && a.pointer_x==450 && a.pointer_y==190);
    e.type=SDL_FINGERDOWN; e.tfinger.x=0.5f; e.tfinger.y=0.5f;
    assert(ls_input(&a,&e)==LS_POINTER && a.pointer_x==320 && a.pointer_y==240);
    int index=SDL_JoystickAttachVirtual(SDL_JOYSTICK_TYPE_UNKNOWN,2,8,1);
    assert(index>=0); (void)drain(&a,LS_NONE);
    SDL_Joystick *virtual=SDL_JoystickOpen(index);
    assert(virtual);
    LS_InputState state = {0};
    assert(!SDL_JoystickSetVirtualButton(virtual,0,1));
    assert(drain(&a,LS_ACTIVATE));
    ls_input_poll(&a, &state);
    assert(state.held & LS_BUTTON_ACTIVATE);
    assert(state.pressed & LS_BUTTON_ACTIVATE);
    assert(!SDL_JoystickSetVirtualButton(virtual,0,0)); (void)drain(&a,LS_NONE);
    assert(!SDL_JoystickSetVirtualHat(virtual,0,SDL_HAT_RIGHT));
    assert(drain(&a,LS_RIGHT));
    assert(!SDL_JoystickSetVirtualAxis(virtual,0,-32768));
    assert(drain(&a,LS_LEFT));
    ls_input_poll(&a, &state);
    int slot = -1;
    for (int i = 0; i < LS_INPUT_PADS; ++i) if (state.pads[i].connected) slot = i;
    assert(slot >= 0 && state.pads[slot].axes_available == 3u);
    assert(state.pads[slot].axes[0] == -1.0f && !state.pads[slot].mapped);
    assert(!SDL_JoystickSetVirtualAxis(virtual, 0, 32767)); (void)drain(&a, LS_NONE);
    ls_input_poll(&a, &state); assert(state.pads[slot].axes[0] == 1.0f);
    assert(!SDL_JoystickSetVirtualAxis(virtual, 0, 16384)); (void)drain(&a, LS_NONE);
    ls_input_poll(&a, &state); assert(state.pads[slot].axes[0] > 0.5f && state.pads[slot].axes[0] < 0.501f);
    assert(!SDL_JoystickDetachVirtual(index)); (void)drain(&a,LS_NONE);
    SDL_JoystickClose(virtual);
    ls_input_poll(&a, &state);
    assert(!state.held && state.released & LS_BUTTON_RIGHT);
    assert(!state.pads[slot].connected && !state.pads[slot].axes_available);
    index = SDL_JoystickAttachVirtual(SDL_JOYSTICK_TYPE_GAMECONTROLLER, 6, 16, 0);
    assert(index >= 0); (void)drain(&a, LS_NONE);
    virtual = SDL_JoystickOpen(index); assert(virtual && SDL_IsGameController(index));
    assert(!SDL_JoystickSetVirtualAxis(virtual, 0, -32768));
    assert(!SDL_JoystickSetVirtualAxis(virtual, 4, 32767)); (void)drain(&a, LS_NONE);
    ls_input_poll(&a, &state);
    slot = -1;
    for (int i = 0; i < LS_INPUT_PADS; ++i) if (state.pads[i].connected) slot = i;
    assert(slot >= 0 && state.pads[slot].mapped);
    assert((state.pads[slot].axes_available & 17u) == 17u);
    assert(state.pads[slot].axes[0] == -1.0f && state.pads[slot].axes[4] == 1.0f);
    assert(!SDL_JoystickDetachVirtual(index)); (void)drain(&a, LS_NONE);
    SDL_JoystickClose(virtual);
    ls_input_poll(&a, &state); assert(!state.held && state.released & LS_BUTTON_LEFT);
    assert(a.joystick_events>=3);
    assert(!ls_audio_pump(&a,1,35));
    SDL_Delay(40);
    assert(!ls_audio_pump(&a,1,35));
    assert(a.tone_queued && a.nonzero_samples && a.capture_frames);
    assert(!ls_audio_pump(&a,0,35));
    assert(!SDL_GetQueuedAudioSize(a.audio));
    snprintf(wav,sizeof wav,"%s/tone.wav",root); assert(!ls_audio_wav(&a,wav));
    snprintf(image,sizeof image,"%s/frame.bmp",root);
    ls_begin(&a,0xf0f0f0); ls_fill(&a,20,20,200,120,0x008c89);
    ls_text(&a,"REAL SDL2",30,160,3,0x20282b);
    assert(!ls_screenshot(&a,image));
    ls_present(&a);
    LS_Settings s={7,1,55},loaded={0,0,35};
    assert(!ls_settings_save(&s));
    assert(ls_settings_load(&loaded)==1 && loaded.color==7 && loaded.tone==1 && loaded.volume==55);
    assert(!ls_data_path(path,sizeof path,"studio.ini"));
    FILE *f=fopen(path,"w"); assert(f);
    fputs("LILSMOKE_STUDIO_1\ncolor=99999999999999999999999999999999\ntone=1\nvolume=35\n",f); fclose(f);
    loaded.color=0; assert(ls_settings_load(&loaded)==-1 && loaded.color==0);
    assert(ls_settings_load(NULL)==-1);
    assert(ls_data_path(path,sizeof path,"../escape")==-1);
    s.color=8; assert(ls_settings_save(&s)==-1);
    ls_app_close(&a);
    assert(!a.window && !a.renderer && !a.audio && !a.capture);
    printf("PASS SDL software frame, native digital keys, input snapshots/aliases/repeat/focus, raw/mapped analog endpoints/triggers/hotplug, mouse/touch, queued PCM, settings bounds, teardown\n");
    /* All generated paths are inside the mkdtemp root above. */
    unlink(image); unlink(wav);
    snprintf(path,sizeof path,"%s/userdata/studio.ini",root); unlink(path);
    snprintf(path,sizeof path,"%s/userdata",root); rmdir(path); rmdir(root);
    return 0;
}
