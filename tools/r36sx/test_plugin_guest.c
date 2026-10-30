/* SPDX-License-Identifier: MPL-2.0 */
#include <assert.h>
#include <dlfcn.h>
#include <stdbool.h>
#include <stdio.h>
int main(int argc,char **argv){
    assert(argc==2);
    void *core=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL);
    if(!core){fprintf(stderr,"dlopen: %s\n",dlerror());return 1;}
    bool (*load)(const void *)=dlsym(core,"retro_load_game");
    assert(load);
    if(load(NULL)){fputs("unexpected return after exec\n",stderr);return 1;}
    puts("PASS MIPS plugin failed exec returned false");
    return dlclose(core)!=0;
}
