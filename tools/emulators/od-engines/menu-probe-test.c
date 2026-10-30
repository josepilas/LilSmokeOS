#include <SDL.h>
#include <assert.h>
#include <stdio.h>
int od_menu_qa_nonblank(SDL_Surface *surface);
int main(void)
{
    SDL_Surface *surface=SDL_CreateRGBSurfaceWithFormat(0,320,240,32,SDL_PIXELFORMAT_ARGB8888);
    assert(surface);
    SDL_FillRect(surface,NULL,SDL_MapRGB(surface->format,255,0,0));
    assert(od_menu_qa_nonblank(surface)==0);
    SDL_Rect pixel={10,10,1,1};
    SDL_FillRect(surface,&pixel,SDL_MapRGB(surface->format,0,0,0));
    assert(od_menu_qa_nonblank(surface)==1);
    SDL_FreeSurface(surface);
    puts("PASS menu probe rejects uniform multibyte-color frames");
    return 0;
}
