/* SPDX-License-Identifier: MPL-2.0 */
#define _GNU_SOURCE
#include "gamepad-state.h"
#include <stdio.h>
int main(int argc,char **argv) {
    unsigned duration=argc>1?(unsigned)atoi(argv[1]):25;
    int fd=open("/run/lilsmoke-gamepads.state",O_RDWR|O_CREAT|O_TRUNC,0644);
    if(fd<0 || ftruncate(fd,sizeof(struct ls_gp_state)))return 1;
    struct ls_gp_state *s=mmap(NULL,sizeof(*s),PROT_READ|PROT_WRITE,MAP_SHARED,fd,0);
    if(s==MAP_FAILED)return 2;
    memset(s,0,sizeof(*s));s->magic=LS_GP_MAGIC;s->abi=1;s->pid=getpid();
    uint64_t start=0,deadline=ls_gp_now()+180000;
    while(ls_gp_now()<deadline && (!start || ls_gp_now()-start<(uint64_t)duration*1000)) {
        uint64_t now=ls_gp_now(),elapsed=start?now-start:0;
        if(!start) {
            unsigned frames=0;FILE *f=fopen("/tmp/treefrog-driver-report.txt","r");
            if(f) { if(fscanf(f,"frames=%u",&frames)!=1)frames=0;fclose(f); }
            if(frames)start=now;
        }
        __atomic_add_fetch(&s->sequence,1,__ATOMIC_ACQ_REL);
        s->millis=now;s->pads[2].connected=1;s->pads[2].instance=77;
        s->pads[2].buttons=0;
        memset(s->pads[2].axes,0,sizeof(s->pads[2].axes));
        if(elapsed>6000 && elapsed<6500)s->pads[2].buttons=1u<<LS_GP_A;
        if(elapsed>9000 && elapsed<9200)s->pads[2].buttons=1u<<LS_GP_A;
        if(elapsed>12000 && elapsed<12400)s->pads[2].axes[0]=32767;
        if((start && elapsed>(uint64_t)(duration-2)*1000) || now>deadline-2000) {
            s->pads[2].buttons=(1u<<LS_GP_START)|(1u<<LS_GP_BACK);
            if(access("/tmp/treefrog-combo-sent",F_OK)) {
                FILE*f=fopen("/tmp/treefrog-combo-sent","w");
                if(f){fputs("Start+Select slot=2\n",f);fclose(f);}
            }
        }
        __atomic_add_fetch(&s->sequence,1,__ATOMIC_RELEASE);
        usleep(8000);
    }
    __atomic_add_fetch(&s->sequence,1,__ATOMIC_ACQ_REL);
    memset(s->pads,0,sizeof(s->pads));s->millis=ls_gp_now();
    __atomic_add_fetch(&s->sequence,1,__ATOMIC_RELEASE);
    munmap(s,sizeof(*s));close(fd);
    puts("TREEFROG_QA_PAD_DONE slot=2 A movement StartSelect release");
    return 0;
}
