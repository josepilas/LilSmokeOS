// SPDX-License-Identifier: MPL-2.0
#include "Input.h"
#include "treefrog-input-map.h"
#include <SDL2/SDL.h>
#include <memory>

static std::unique_ptr<lilsmoke::Input> input;

extern "C" void treefrog_input_init(void) {
    SDL_Init(SDL_INIT_EVENTS);
    input=std::make_unique<lilsmoke::Input>();
    input->init(640,480);
    input->disconnectLegacySharedInput();
    input->setPointerOptions(false,"normal");
}

extern "C" uint32_t treefrog_input_read(void) {
    if(!input)return 0;
    SDL_Event event;
    while(SDL_PollEvent(&event))input->handleSDLEvent(event);
    for(unsigned n=0;n<256;++n) {
        lilsmoke::InputButton button;lilsmoke::InputEvent edge;lilsmoke::PointerEvent pointer;
        if(!input->poll(button,edge,pointer))break;
    }
    const auto s=input->gameplayState();
    const auto a=input->analogState();
    const bool held[]={s.a,s.b,s.x,s.y,s.select,s.home,s.start,s.l3,s.r3,
        s.l1,s.r1,s.up,s.down,s.left,s.right};
    uint32_t buttons=0;
    for(unsigned i=0;i<15;++i)if(held[i])buttons|=1u<<i;
    int16_t axes[6]={static_cast<int16_t>(a.hasLeft?a.leftX:0),
        static_cast<int16_t>(a.hasLeft?a.leftY:0),0,0,
        static_cast<int16_t>(s.l2?32767:0),static_cast<int16_t>(s.r2?32767:0)};
    return treefrog_pad(buttons,axes);
}

extern "C" void treefrog_input_close(void) {input.reset();SDL_Quit();}
