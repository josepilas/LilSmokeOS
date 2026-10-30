// SPDX-License-Identifier: MPL-2.0
#include "profile-input.cpp"
#include <cassert>
#include <unistd.h>

namespace lilsmoke {
struct InputTestAccess {
    static void keyboard(Input &in) {
        Input::Device device;device.fd=-2;device.path="profile-keyboard";device.keyboard=true;
        in.devices_.push_back(std::move(device));in.inputDir_="/nonexistent/treefrog-profile-test";
    }
    static void key(Input &in,int code,bool down) {
        auto &device=in.devices_.front();auto action=in.deviceButton(device,(unsigned)code);
        in.sourceButton(device,code,action,down?InputEvent::Pressed:InputEvent::Released);
    }
};
}

static void controller_cases() {
    const int16_t neutral[2]={0};
    // Every original button survives the opt-in adapter; no face-role swapping.
    for(uint32_t raw=0;raw<(1u<<17);raw++)
        assert((chocolate_doom_controls(raw,neutral)&raw)==raw);
    const int16_t left[2]={-32768,0},right[2]={32767,0},up[2]={0,-32768},down[2]={0,32767};
    const int16_t deadzone[2]={-14000,14000};
    assert(chocolate_doom_controls(0,left)==1u<<7);
    assert(chocolate_doom_controls(0,right)==1u<<5);
    assert(chocolate_doom_controls(0,up)==0 && chocolate_doom_controls(0,down)==0);
    assert(chocolate_doom_controls(0,deadzone)==0);
    assert(chocolate_doom_controls(1u<<8,neutral)==((1u<<8)|(1u<<12)|(1u<<7)));
    assert(chocolate_doom_controls(1u<<9,neutral)==((1u<<9)|(1u<<12)|(1u<<5)));
    assert(chocolate_doom_controls((1u<<8)|(1u<<9),neutral)==((1u<<8)|(1u<<9)|(1u<<12)|(1u<<7)|(1u<<5)));
    assert(chocolate_doom_controls((1u<<10)|(1u<<11),neutral)==((1u<<10)|(1u<<11)));
    assert(chocolate_doom_controls(1u<<1,neutral)==((1u<<1)|(1u<<15)));
    assert(chocolate_doom_controls(1u<<2,neutral)==((1u<<2)|(1u<<14)));
    // Returning to neutral releases synthetic actions; no profile-held latch.
    assert(chocolate_doom_controls(0,neutral)==0);
    assert(chocolate_doom_controls(9,neutral)==9);
    assert(chocolate_doom_controls(1u<<3|1u<<12,neutral)==(1u<<3|1u<<12));
    for(unsigned i=0;i<17;i++)assert(classicube_sticks(1u<<i,neutral)==(1u<<i));
    assert(classicube_sticks(1u<<10,right)==((1u<<10)|(1u<<13)));
}
static void keyboard_cases() {
    using A=lilsmoke::InputTestAccess;
    input=std::make_unique<lilsmoke::Input>();A::keyboard(*input);
    setenv("LILSMOKE_TREEFROG_INPUT_PROFILE","chocolate-doom",1);
    setenv("LILSMOKE_GAMEPAD_STATE","/nonexistent/treefrog-profile-test",1);
    native_keyboard_profile();
    const int codes[]={KEY_UP,KEY_DOWN,KEY_LEFT,KEY_RIGHT,KEY_LEFTCTRL,KEY_RIGHTCTRL,KEY_ENTER,
        KEY_SPACE,KEY_BACKSPACE,KEY_LEFTALT,KEY_RIGHTALT,KEY_LEFTSHIFT,KEY_RIGHTSHIFT,
        KEY_LEFTBRACE,KEY_N,KEY_RIGHTBRACE,KEY_Y,KEY_ESC,KEY_TAB};
    const unsigned bits[]={4,6,7,5,13,13,13,14,14,12,12,15,15,10,10,11,11,3,0};
    for(unsigned i=0;i<sizeof codes/sizeof codes[0];i++) {
        A::key(*input,codes[i],true);assert(treefrog_input_read()==1u<<bits[i]);
        A::key(*input,codes[i],false);assert(treefrog_input_read()==0);
    }
    A::key(*input,KEY_LEFTCTRL,true);A::key(*input,KEY_ENTER,true);
    A::key(*input,KEY_LEFTCTRL,false);assert(treefrog_input_read()==1u<<13);
    A::key(*input,KEY_ENTER,false);assert(treefrog_input_read()==0);
    // A caller-supplied ordinary Keymap file wins without changing global maps.
    char path[]="/tmp/treefrog-doom-keymap-XXXXXX";int fd=mkstemp(path);assert(fd>=0);
    const char map[]="evdev 57 l1\n";assert(write(fd,map,sizeof map-1)==sizeof map-1);close(fd);
    setenv("LILSMOKE_TREEFROG_KEYMAP",path,1);native_keyboard_profile();
    A::key(*input,KEY_SPACE,true);assert(treefrog_input_read()==1u<<10);
    A::key(*input,KEY_SPACE,false);assert(treefrog_input_read()==0);
    unlink(path);unsetenv("LILSMOKE_TREEFROG_KEYMAP");
    input.reset();unsetenv("LILSMOKE_TREEFROG_INPUT_PROFILE");
}
int main() {
    assert(SDL_Init(SDL_INIT_EVENTS)==0);controller_cases();keyboard_cases();SDL_Quit();
    puts("CHOCOLATE_DOOM_PROFILE 131072 masks/camera/strafe/LR/triggers/clicks/release/19-evdev-keys/remap/aggregation passed");
}
