/* SPDX-License-Identifier: MPL-2.0 */
#define R36_BOOTSTRAP_TEST
#include "../../board/r36sx-2.7-stock/bootstrap_exec.c"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static int calls,flags3,flags7=1;
long r36_test_syscall(long number,long a,long b,long c){
    if(number==4055){
        assert(a>=3&&a<1024);
        int *flags=a==3?&flags3:a==7?&flags7:NULL;
        if(b==1)return flags?*flags:-9;
        assert(b==2&&flags);*flags=(int)c;return 0;
    }
    assert(number==4011);
    assert(flags3==1&&flags7==1);
    assert(!strcmp((const char *)a,"/bin/sh"));
    const char *const *args=(const char *const *)b;
    const char *const *env=(const char *const *)c;
    assert(!strcmp(args[0],"sh"));
    assert(!strcmp(args[1],"/mnt/sdcard/lilsmoke/r36-launch.sh"));
    assert(args[2]==NULL);
    assert(!strcmp(env[0],"PATH=/bin:/sbin:/usr/bin:/usr/sbin"));
    assert(!strcmp(env[1],"LILSMOKE_R36_BOOTSTRAP_MODE=exec"));
    assert(env[2]==NULL);
    calls++;return -2;
}
int main(void){
    struct retro_system_info system;
    struct retro_system_av_info av;
    retro_get_system_info(&system);retro_get_system_av_info(&av);
    assert(av.geometry.base_width==640&&av.geometry.base_height==480);
    assert(!retro_load_game(NULL)&&calls==1);
    assert(flags3==0&&flags7==1);
    puts("PASS freestanding bootstrap execve number, argv, clean environment, failed-exec safe return, panel geometry");
    return 0;
}
