/* SPDX-License-Identifier: MPL-2.0
 * VM-only controller actor. Uses production input transport, no host input. */
#define _POSIX_C_SOURCE 200809L
#include "gamepad-state.h"
#include <stdio.h>
#include <signal.h>
static volatile sig_atomic_t stop;
static void quit(int s){(void)s;stop=1;}
int main(void){
    signal(SIGTERM,quit);int fd=open("/run/lilsmoke-gamepads.state",O_RDWR|O_CREAT|O_TRUNC,0600);if(fd<0)return 1;
    if(ftruncate(fd,sizeof(struct ls_gp_state)))return 2;
    struct ls_gp_state*s=mmap(NULL,sizeof(*s),PROT_READ|PROT_WRITE,MAP_SHARED,fd,0);if(s==MAP_FAILED)return 3;
    memset(s,0,sizeof(*s));s->magic=LS_GP_MAGIC;s->abi=1;s->pid=(uint32_t)getpid();s->pads[0].connected=1;s->pads[0].instance=1;
    uint64_t start=0;unsigned previous=99;
    while(!stop){
        uint64_t now=ls_gp_now();if(!start&&!access("/tmp/treefrog-driver-report.txt",F_OK))start=now;
        unsigned phase=start?(unsigned)((now-start)/600)%8:99;
        __atomic_add_fetch(&s->sequence,1,__ATOMIC_RELEASE);s->millis=now;s->pads[0].buttons=0;s->pads[0].axes[0]=0;
        if(phase==1)s->pads[0].buttons=1u<<LS_GP_START;
        if(phase==3)s->pads[0].buttons=1u<<LS_GP_A;
        if(phase==5)s->pads[0].axes[0]=32767;
        __atomic_add_fetch(&s->sequence,1,__ATOMIC_RELEASE);
        if(phase!=previous){printf("VM controller phase=%u buttons=%u left_x=%d\n",phase,s->pads[0].buttons,s->pads[0].axes[0]);fflush(stdout);previous=phase;}
        struct timespec delay={0,20000000};nanosleep(&delay,NULL);
    }
    munmap(s,sizeof(*s));close(fd);return 0;
}
