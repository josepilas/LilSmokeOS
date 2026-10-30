#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void require(int ok, const char *name) {
    if (!ok) { fprintf(stderr,"FAIL %s: %s\n",name,SDL_GetError()); exit(1); }
}
int main(void) {
    const Uint32 formats[] = {SDL_PIXELFORMAT_RGB565, SDL_PIXELFORMAT_RGB24,
                             SDL_PIXELFORMAT_BGR24, SDL_PIXELFORMAT_ARGB8888};
    unsigned checks=0, rounding_differences=0;
    require(SDL_Init(0)==0,"SDL init");
    for (unsigned f=0;f<4;f++) for (unsigned rgb32=0;rgb32<2;rgb32++) {
        Uint32 input_format=rgb32?SDL_PIXELFORMAT_RGB888:SDL_PIXELFORMAT_RGB565;
        SDL_Surface *input=SDL_CreateRGBSurfaceWithFormat(0,257,223,0,input_format);
        SDL_Surface *reference=SDL_CreateRGBSurfaceWithFormat(0,640,480,0,formats[f]);
        SDL_Surface *output=SDL_CreateRGBSurfaceWithFormat(0,640,480,0,formats[f]);
        require(input && reference && output,"surfaces");
        SDL_SetSurfaceBlendMode(input,SDL_BLENDMODE_NONE);
        SDL_Renderer *renderer=SDL_CreateSoftwareRenderer(output);
        require(renderer!=NULL,"software renderer");
        SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY,"nearest");
        SDL_Texture *texture=SDL_CreateTexture(renderer,formats[f],SDL_TEXTUREACCESS_STREAMING,257,223);
        require(texture!=NULL,"native texture");
        SDL_SetTextureBlendMode(texture,SDL_BLENDMODE_NONE);
        SDL_Rect viewport={23,17,594,446};
        SDL_RenderSetViewport(renderer,&viewport);
        SDL_SetRenderDrawColor(renderer,0,0,0,255);
        SDL_RenderClear(renderer);
        for(unsigned frame=0;frame<3;frame++) {
            for(int y=0;y<input->h;y++) for(int x=0;x<input->w;x++) {
                Uint32 color=SDL_MapRGB(input->format,(x*13+frame*31)&255,(y*7)&255,(x+y+frame*83)&255);
                char *p=(char *)input->pixels+y*input->pitch+x*input->format->BytesPerPixel;
                if(rgb32) {
                    /* Libretro XRGB8888 leaves the top byte unspecified. */
                    color=(color&0xffffffu)|((frame&1)?0xa5000000u:0);
                    memcpy(p,&color,4);
                } else { Uint16 c=color; memcpy(p,&c,2); }
            }
            SDL_FillRect(reference,NULL,SDL_MapRGB(reference->format,0,0,0));
            SDL_Rect dst=viewport;
            require(SDL_BlitScaled(input,NULL,reference,&dst)==0,"reference scale");
            void *pixels; int pitch;
            require(SDL_LockTexture(texture,NULL,&pixels,&pitch)==0,"texture lock");
            require(SDL_ConvertPixels(257,223,input_format,input->pixels,input->pitch,formats[f],pixels,pitch)==0,"source resolution conversion");
            SDL_UnlockTexture(texture);
            require(SDL_RenderCopy(renderer,texture,NULL,NULL)==0,"native format scale");
            SDL_RenderPresent(renderer);
            for(int y=0;y<480;y++) {
                for(int b=0;b<640*output->format->BytesPerPixel;b++) {
                        unsigned char r=((unsigned char *)reference->pixels)[y*reference->pitch+b];
                        unsigned char o=((unsigned char *)output->pixels)[y*output->pitch+b];
                        /* SDL's RGB565-to-32 LUT and generic scaled blitter
                         * round green differently by one 8-bit level. */
                        int tolerance=(!rgb32 && formats[f]==SDL_PIXELFORMAT_ARGB8888 && b%4==1)?1:0;
                        if(r!=o) rounding_differences++;
                        require(abs((int)r-(int)o)<=tolerance,"pixel equivalence including borders");
                }
            }
            checks++;
        }
        size_t bytes=(size_t)viewport.w*viewport.h*3;
        unsigned char *capture=malloc(bytes+32);
        require(capture!=NULL,"capture allocation");
        memset(capture,0xa5,bytes+32);
        require(SDL_RenderReadPixels(renderer,&viewport,SDL_PIXELFORMAT_BGR24,capture+16,viewport.w*3)==0,"viewport readback");
        for(unsigned i=0;i<16;i++)
            require(capture[i]==0xa5 && capture[bytes+16+i]==0xa5,"capture bounds");
        checks++;
        free(capture); SDL_DestroyTexture(texture); SDL_DestroyRenderer(renderer);
        SDL_FreeSurface(input); SDL_FreeSurface(reference); SDL_FreeSurface(output);
    }
    SDL_Quit(); printf("SOFTWARE_VIDEO_SMOKE_OK checks=%u green_rounding_differences=%u\n",checks,rounding_differences); return 0;
}
