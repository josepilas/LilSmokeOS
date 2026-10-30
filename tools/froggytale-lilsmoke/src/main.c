/* SPDX-License-Identifier: AGPL-3.0-only */
#include "port.h"
#include "loop.h"
#include "sw_renderer.h"
#include "cli/args.h"
#include <sys/stat.h>
#include <errno.h>
#include <unistd.h>
extern int ft_prepare(const char*,const char*,bool,char*,size_t);
extern void ft_text(Renderer*,const char*,int,int,int,uint32_t);

static void message(Renderer* r,const char* status) {
    r->vtable->beginFrame(r,320,240,320,240);
    r->vtable->beginGUI(r,320,240,0,0,320,240,-1);
    r->vtable->clearScreen(r,0x201810,1);
    ft_text(r,"Undertale-LilSmoke",12,18,2,0xffffff);
    ft_text(r,status,12,65,1,0xffffff);
    ft_text(r,"Experimental 0.1.0\nAGPLv3 / NO WARRANTY\nLicense: licenses/AGPL-3.0.txt\nSource: SOURCE-OFFER.txt",12,145,1,0xffffff);
    r->vtable->endGUI(r);SwRenderer_present();
}
int main(int argc,char** argv) {
    setbuf(stdout,NULL);setbuf(stderr,NULL);
    if(argc>=2 && !strcmp(argv[1],"--self-test")) return ft_self_test(argc>2?argv[2]:NULL);
    if(argc==2 && !strcmp(argv[1],"--version")) {puts("Undertale-LilSmoke 0.1.0 experimental CPU-only WAD16 SDL audio AGPLv3");return 0;}
    if(argc==2 && !strcmp(argv[1],"--help")) {
        puts("froggytale-lilsmoke [WAD [runner options]]\n--self-test [frame.ppm] | --check-data WAD | --version\nNo WAD: original/data.win, original/game.unx or original/game.ios.\nFirst run prepares the software atlases; later runs reuse the cache.\nPut the game's external OGG music files beside the original WAD.\nPhysical A=Z, B=X, X/Y/Select=C, arrows/stick=movement, Start=Enter, Start+Select=exit.\nLicense and corresponding source: licenses/ and SOURCE-OFFER.txt. No warranty.");return 0;
    }
    if(argc==3 && !strcmp(argv[1],"--check-data")) {char e[160];int ret=ft_validate_data(argv[2],e,sizeof e);puts(ret?e:"BYO WAD16/atlas structural check passed (not gameplay qualification)");return ret;}
    bool prepare_only=argc>=3 && !strcmp(argv[1],"--prepare-only");
    const char* path=prepare_only?argv[2]:argc>1?argv[1]:"original/data.win";
    if(argc==1 && access(path,R_OK)) {
        if(!access("original/game.unx",R_OK))path="original/game.unx";
        else if(!access("original/game.ios",R_OK))path="original/game.ios";
    }
    bool headless=getenv("FROGGY_NONINTERACTIVE")!=NULL;
    for(int i=2;i<argc;++i)if(!strcmp(argv[i],"--headless"))headless=true;
    const char* cache=getenv("FROGGY_CACHE_DIR");if(!cache)cache="cache";
    char prepared[600];
    if(!access(path,R_OK)) {
        int prep=ft_prepare(path,cache,headless,prepared,sizeof prepared);
        if(prep)return prep;
        if(prepare_only)return 0;
        setenv("SW_ASSET_DIR",prepared,1);SwRenderer_setAssetDir(prepared);
    } else if(prepare_only) {fprintf(stderr,"Original is missing: %s\n",path);return 66;}
    char error[160];
    if(ft_validate_data(path,error,sizeof error)) {
        fprintf(stderr,"%s: %s\n",path,error);
        if(getenv("FROGGY_NONINTERACTIVE")) return 66;
        if(!ft_platform_open(false)) return 69;
        Renderer* r=SWRenderer_create();message(r,"Place your data.win in original/\nThen launch again. No game data is included.");
        while(!ft_input_poll()) {SDL_Delay(20);const Uint8* k=SDL_GetKeyboardState(NULL);if(k[SDL_SCANCODE_ESCAPE])break;}
        r->vtable->destroy(r);ft_platform_close();return 66;
    }
    const char* saves=getenv("FROGGY_SAVE_DIR");if(!saves)saves="saves";
    if(mkdir(saves,0700) && errno!=EEXIST) {perror("save directory");return 73;}
    /* Defaults are appended after caller options: this executable cannot select GL. */
    char** argsv=calloc((size_t)argc+16,sizeof(char*));int n=0;
    argsv[n++]=argv[0];argsv[n++]=(char*)path;
    for(int i=2;i<argc;++i) argsv[n++]=argv[i];
    const char* defaults[]={"--renderer","software","--lazy-rooms","--lazy-textures","--lazy-audio",
        "--load-type","map-file","--save-folder",saves,"--disable-log-colours"};
    for(unsigned i=0;i<sizeof defaults/sizeof defaults[0];++i) argsv[n++]=(char*)defaults[i];
    CommandLineArgs options;parseCommandLineArgs(&options,n,argsv);free(argsv);
    int ret=loop(options,argv[0]);freeCommandLineArgs(&options);
    printf("FROGGY_EXIT status=%d frames=%u frame_hash=%08x\n",ret,ft_frames,ft_frame_hash);return ret;
}
