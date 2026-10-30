// SPDX-License-Identifier: MPL-2.0
// Retain the production bridge; native adaptations are explicitly selected.
#define treefrog_input_init treefrog_input_init_default
#define treefrog_input_read treefrog_input_read_default
#include "../../package/lilsmoke-scripts/src/treefrog-common-input.cpp"
#undef treefrog_input_init
#undef treefrog_input_read
#include "classicube-input.h"
#include "chocolate-doom-input.h"
#include <cstdlib>
#include <cstring>
#include <cstdio>

static bool chocolate_doom_profile() {
    const char *profile=std::getenv("LILSMOKE_TREEFROG_INPUT_PROFILE");
    return profile && !std::strcmp(profile,"chocolate-doom");
}

static void native_keyboard_profile() {
    if(!input)return;
    if(chocolate_doom_profile()) {
        // Linux evdev key codes, not SDL scancodes. No cross-process keyboard
        // events are injected; the native SDL keyboard path remains untouched.
        using B=lilsmoke::InputButton;
        const struct {int code;B action;} keys[]={
            {KEY_UP,B::Up},{KEY_DOWN,B::Down},{KEY_LEFT,B::Left},{KEY_RIGHT,B::Right},
            {KEY_LEFTCTRL,B::A},{KEY_RIGHTCTRL,B::A},{KEY_ENTER,B::A},
            {KEY_SPACE,B::B},{KEY_BACKSPACE,B::B},
            {KEY_LEFTALT,B::X},{KEY_RIGHTALT,B::X},{KEY_LEFTSHIFT,B::Y},{KEY_RIGHTSHIFT,B::Y},
            {KEY_LEFTBRACE,B::L1},{KEY_N,B::L1},{KEY_RIGHTBRACE,B::R1},{KEY_Y,B::R1},
            {KEY_ESC,B::Start},{KEY_TAB,B::Select}};
        for(const auto &key:keys)input->keymap().set(key.code,key.action);
        // Explicit user overrides still win over the profile's defaults.
        input->keymap().loadDir("/persist/config/input-maps");
    }
    const char *map=std::getenv("LILSMOKE_TREEFROG_KEYMAP");
    if(map && *map && input->keymap().loadFile(map)<0)
        std::fprintf(stderr,"TREEFROG_INPUT_KEYMAP cannot_load=%s\n",map);
}

extern "C" void treefrog_input_init(void) {
    treefrog_input_init_default();native_keyboard_profile();
    const char *profile=std::getenv("LILSMOKE_TREEFROG_INPUT_PROFILE");
    std::fprintf(stderr,"TREEFROG_INPUT_PROFILE name=%s\n",profile?profile:"canonical");
}

extern "C" uint32_t treefrog_input_read(void) {
    const auto raw = treefrog_input_read_default();
    const char *profile = std::getenv("LILSMOKE_TREEFROG_INPUT_PROFILE");
    if (!input || !profile || (std::strcmp(profile,"classicube") && !chocolate_doom_profile())) return raw;
    const auto a = input->analogState();
    const int16_t axes[] = {static_cast<int16_t>(a.hasRight ? a.rightX : 0),
        static_cast<int16_t>(a.hasRight ? a.rightY : 0)};
    return chocolate_doom_profile()?chocolate_doom_controls(raw,axes):classicube_sticks(raw,axes);
}
