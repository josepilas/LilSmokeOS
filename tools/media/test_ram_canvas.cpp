#include "Renderer.h"
#include "Font.h"
#include <SDL2/SDL.h>
#include <cassert>
#include <cstdio>
#include <vector>
int main(int argc,char **argv){
    assert(argc==3);
    setenv("SDL_VIDEODRIVER","deliberately-unavailable",1);
    setenv("LILSMOKE_UI_FAST_BLITS","1",1);
    lilsmoke::Renderer renderer;assert(renderer.initOffscreen(320,240));
    assert(!(SDL_WasInit(SDL_INIT_VIDEO)&SDL_INIT_VIDEO));
    renderer.clear(0);
    auto *sdl=static_cast<SDL_Renderer*>(renderer.sdlRenderer());
    std::vector<uint32_t> pixels(320*240,~0u);
    assert(!SDL_RenderReadPixels(sdl,nullptr,SDL_PIXELFORMAT_ARGB8888,pixels.data(),320*4));
    for(auto p:pixels)assert(p==0);
    renderer.fillRect(10,10,30,20,0xff000080);
    renderer.fillRect(50,10,30,20,0x00ff00ff);
    lilsmoke::Font font;assert(font.load(argv[1],16));
    font.render(renderer,"Video controls",10,45,0xffffffff);
    auto *image=renderer.loadImage(argv[2]);assert(image);
    renderer.drawTexture(image,10,80,120,80);
    assert(!SDL_RenderReadPixels(sdl,nullptr,SDL_PIXELFORMAT_ARGB8888,pixels.data(),320*4));
    assert(pixels[0]==0&&pixels.back()==0);
    assert((pixels[15*320+15]>>24)>=127&&(pixels[15*320+15]>>24)<=129);
    assert(pixels[15*320+55]==0xff00ff00u);
    unsigned visible=0;for(auto p:pixels)visible+=(p>>24)!=0;
    assert(visible>9000);
    assert(!(SDL_WasInit(SDL_INIT_VIDEO)&SDL_INIT_VIDEO));
    std::printf("MEDIA_RAM_CANVAS_PASS no_video_driver=1 transparent_clear=1 alpha=1 text=1 webp=1 pixels=%u\n",visible);
}
