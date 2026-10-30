#define _GNU_SOURCE
#include "gamepad-state.h"
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <sys/mman.h>
#include <unistd.h>
static volatile sig_atomic_t stop;
static void stopping(int number) {(void)number;stop=1;}
int main(void) {
    const char *path=getenv("LILSMOKE_GAMEPAD_STATE");
    if(!path)path="/run/lilsmoke-gamepads.state";
    int fd=open(path,O_CREAT|O_EXCL|O_RDWR,0600);
    if(fd<0 || ftruncate(fd,sizeof(struct ls_gp_state)))return 1;
    struct ls_gp_state *state=mmap(NULL,sizeof(*state),PROT_READ|PROT_WRITE,MAP_SHARED,fd,0);
    if(state==MAP_FAILED)return 1;
    state->magic=LS_GP_MAGIC;state->abi=1;state->pid=getpid();
    signal(SIGTERM,stopping);signal(SIGINT,stopping);
    while(!stop) {
        __atomic_add_fetch(&state->sequence,1,__ATOMIC_ACQ_REL);
        state->millis=ls_gp_now();
        for(unsigned port=0;port<4;++port) {
            struct ls_gp_pad *pad=&state->pads[port];pad->connected=1;
            pad->vendor=0x045e;pad->product=0x028e;
            snprintf(pad->name,sizeof(pad->name),"Analog QA Fixture %u",port);
            pad->axes[0]=24000;pad->axes[1]=-22000;pad->axes[2]=-18000;pad->axes[3]=26000;
        }
        __atomic_add_fetch(&state->sequence,1,__ATOMIC_RELEASE);usleep(8000);
    }
    munmap(state,sizeof(*state));close(fd);unlink(path);return 0;
}
