/* SPDX-License-Identifier: MPL-2.0
 * No musl headers/CRT: exercise the plugin with the owner's exact glibc loader. */
#include <stddef.h>
#include <stdbool.h>
extern void *dlopen(const char *,int), *dlsym(void *,const char *);
extern char *dlerror(void);
extern int dlclose(void *);
extern long write(int,const void *,unsigned long);
extern void _exit(int);
extern int __libc_start_main(int (*)(int,char **,char **),int,char **,
                            void (*)(void),void (*)(void),void (*)(void),void *);
static unsigned length(const char *s){unsigned n=0;while(s && s[n])n++;return n;}
static int qa_main(int argc,char **argv,char **env){
    (void)env;
    if(argc!=2)return 2;
    void *p=dlopen(argv[1],2);
    if(!p){const char *e=dlerror();write(2,e,length(e));return 3;}
    unsigned (*api)(void)=dlsym(p,"retro_api_version");
    void (*init)(void)=dlsym(p,"retro_init");
    bool (*load)(const void *)=dlsym(p,"retro_load_game");
    if(!api || api()!=1 || !init || !load)return 4;
    init();
    if(load(NULL))return 5;
    if(dlclose(p))return 6;
    const char ok[]="PASS exact stock glibc dlopen, constructor, init, failed exec and return\n";
    write(1,ok,sizeof(ok)-1);
    return 0;
}
void qa_entry(unsigned long *stack){
    (void)__libc_start_main(qa_main,(int)stack[0],(char **)(stack+1),NULL,NULL,NULL,stack);
    _exit(99);
}
__asm__(
    ".text\n.set noreorder\n.globl _start\n.ent _start\n"
    "_start:\n bal 1f\n nop\n1:\n .cpload $31\n"
    "move $4,$29\n addiu $29,$29,-32\n"
    "lw $25,%call16(qa_entry)($28)\n jalr $25\n nop\n"
    "break\n.end _start\n.set reorder\n");
