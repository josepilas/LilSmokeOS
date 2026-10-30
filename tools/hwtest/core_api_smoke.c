#include <dlfcn.h>
#include <stdio.h>
#include "libretro.h"

int main(int argc,char **argv) {
    if(argc!=2) return 2;
    void *core=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL);
    if(!core) { fprintf(stderr,"%s\n",dlerror()); return 1; }
    unsigned (*version)(void)=dlsym(core,"retro_api_version");
    void (*info)(struct retro_system_info *)=dlsym(core,"retro_get_system_info");
    if(!version || !info || version()!=RETRO_API_VERSION) return 1;
    struct retro_system_info system={0}; info(&system);
    if(!system.library_name || !system.library_version) return 1;
    printf("CORE_API_OK name=%s version=%s\n",system.library_name,system.library_version);
    dlclose(core); return 0;
}
