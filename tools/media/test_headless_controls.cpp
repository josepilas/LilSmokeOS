#include "Input.h"
#include "Renderer.h"
#include <SDL2/SDL.h>
#include <cassert>
#include <cstdio>
#include <string>

static void pump(lilsmoke::Input &input) {
    SDL_JoystickUpdate();SDL_PumpEvents();SDL_Event event;
    while(SDL_PollEvent(&event))input.handleSDLEvent(event);
    lilsmoke::InputButton button;lilsmoke::InputEvent action;lilsmoke::PointerEvent pointer;
    for(unsigned i=0;i<128&&input.poll(button,action,pointer);++i){}
}
static int attach() {
    const int index=SDL_JoystickAttachVirtual(SDL_JOYSTICK_TYPE_GAMECONTROLLER,6,15,1);
    assert(index>=0);
    char guid[33];SDL_JoystickGetGUIDString(SDL_JoystickGetDeviceGUID(index),guid,sizeof(guid));
    const std::string map=std::string(guid)+",LilSmoke QA,a:b0,b:b1,x:b2,y:b3,back:b4,guide:b5,start:b6,"
        "leftstick:b7,rightstick:b8,leftshoulder:b9,rightshoulder:b10,"
        "dpup:h0.1,dpright:h0.2,dpdown:h0.4,dpleft:h0.8,"
        "leftx:a0,lefty:a1,rightx:a2,righty:a3,lefttrigger:a4,righttrigger:a5,";
    assert(SDL_GameControllerAddMapping(map.c_str())>=0);
    assert(SDL_IsGameController(index));
    return index;
}
int main(){
    setenv("SDL_VIDEODRIVER","deliberately-unavailable",1);
    setenv("LILSMOKE_INPUT_DIR","/nonexistent-lilsmoke-qa-input",1);
    lilsmoke::Renderer renderer;assert(renderer.initOffscreen(320,240));
    lilsmoke::Input input;input.init(320,240);
    assert(SDL_WasInit(SDL_INIT_GAMECONTROLLER)&&!SDL_WasInit(SDL_INIT_VIDEO));
    for(unsigned iteration=0;iteration<2;++iteration){
        const int index=attach();pump(input);
        SDL_Joystick *joystick=SDL_JoystickOpen(index);assert(joystick);
        assert(SDL_JoystickSetVirtualButton(joystick,0,1)==0);pump(input);
        assert(input.state().a);
        assert(SDL_JoystickSetVirtualButton(joystick,0,0)==0);pump(input);
        assert(!input.state().a);
        assert(SDL_JoystickSetVirtualButton(joystick,6,1)==0);pump(input);
        assert(input.state().start);
        assert(SDL_JoystickDetachVirtual(index)==0);pump(input);
        assert(!input.state().start&&!input.state().a);
        SDL_JoystickClose(joystick);
    }
    assert(!SDL_WasInit(SDL_INIT_VIDEO));
    std::puts("MEDIA_HEADLESS_CONTROLS_PASS buttons=1 release=1 hotplug=2 unplug_release=1 no_video_driver=1 virtual_only=1");
}
