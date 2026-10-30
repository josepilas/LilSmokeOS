#include "EmulatorSettingsView.h"
#include "Renderer.h"
#include "Font.h"
#include <SDL2/SDL.h>
#include <cassert>
#include <fstream>
#include <iostream>
using namespace lilsmoke;
int main(int argc,char **argv) {
    assert(argc==4);
    Config config;
    assert(config.load(argv[1]));
    Renderer renderer; assert(renderer.init(800,450));
    Font font; assert(font.load(argv[2],16));
    Theme theme;
    EmulatorSettingsView settings(config);
    for(unsigned i=0;i<5;++i)settings.handleInput(InputButton::Down,InputEvent::Pressed);
    settings.handleInput(InputButton::A,InputEvent::Pressed);
    settings.handleInput(InputButton::Down,InputEvent::Pressed);
    settings.render(renderer,font,theme);renderer.present();
    int width,height;
    assert(!SDL_GetRendererOutputSize(static_cast<SDL_Renderer *>(renderer.sdlRenderer()),&width,&height));
    auto surface=SDL_CreateRGBSurfaceWithFormat(0,width,height,32,SDL_PIXELFORMAT_ARGB8888);assert(surface);
    assert(!SDL_RenderReadPixels(static_cast<SDL_Renderer *>(renderer.sdlRenderer()),nullptr,SDL_PIXELFORMAT_ARGB8888,surface->pixels,surface->pitch));
    assert(!SDL_SaveBMP(surface,argv[3]));SDL_FreeSurface(surface);
    settings.handleInput(InputButton::A,InputEvent::Pressed);
    assert(config.get("EMULATOR_gba")=="app:org.test.vm-emulator");
    Config saved;assert(saved.load(argv[1]));
    assert(saved.get("EMULATOR_gba")=="app:org.test.vm-emulator");
    std::cout<<"QA_APP_SELECTED_IN_REAL_SETTINGS\n";
}
