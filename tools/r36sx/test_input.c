/* SPDX-License-Identifier: MPL-2.0 */
#define _GNU_SOURCE
#include <fcntl.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
static int test_open(const char *path,int flags,...){
    if(!strcmp(path,"/dev/uinput")||!strcmp(path,"/dev/input/uinput")){errno=ENODEV;return -1;}
    mode_t mode=0;
    if(flags&O_CREAT){va_list args;va_start(args,flags);mode=(mode_t)va_arg(args,int);va_end(args);}
    return openat(AT_FDCWD,path,flags,mode);
}
#define open(...) test_open(__VA_ARGS__)
#define main r36_program_main
#include "../../board/r36sx-2.7-stock/r36-input.c"
#undef main
#undef open
#include <assert.h>
#include <sys/wait.h>

static void integration(void){
    char folder[]="/tmp/r36-input-integration-XXXXXX";
    assert(mkdtemp(folder));
    char source[128],destination[128];
    assert(snprintf(source,sizeof(source),"%s/usb",folder)>0);
    assert(snprintf(destination,sizeof(destination),"%s/panel",folder)>0);
    int usbfd=open(source,O_RDWR|O_CREAT|O_EXCL,0600);assert(usbfd>=0);
    struct ls_gp_state usb;memset(&usb,0,sizeof(usb));
    usb.magic=LS_GP_MAGIC;usb.abi=1;usb.sequence=2;usb.millis=ls_gp_now();
    usb.pads[0].connected=1;usb.pads[0].instance=23;usb.pads[0].axes[1]=12121;
    usb.pads[0].buttons=1U<<LS_GP_B;
    assert(write(usbfd,&usb,sizeof(usb))==(ssize_t)sizeof(usb));
    volatile uint32_t *panel=mmap(NULL,4096,PROT_READ|PROT_WRITE,MAP_SHARED|MAP_ANONYMOUS,-1,0);
    assert(panel!=MAP_FAILED);*panel=1U<<13;
    pid_t child=fork();assert(child>=0);
    if(!child){signal(SIGTERM,quit);_exit(broker(panel,source,destination));}
    assert(setenv("LILSMOKE_GAMEPAD_STATE",destination,1)==0);
    struct ls_gp_reader reader=LS_GP_READER_INIT;
    struct ls_gp_state readback;
    int valid=0;
    for(unsigned i=0;i<40;i++){if(ls_gp_read(&reader,&readback)){valid=1;break;}usleep(10000);}
    assert(valid&&readback.pads[0].instance==23);
    assert(readback.pads[0].buttons==((1U<<LS_GP_A)|(1U<<LS_GP_B)));
    assert(readback.pads[0].axes[1]==12121);
    *panel=0;
    usleep(30000);assert(ls_gp_read(&reader,&readback));
    assert(readback.pads[0].buttons==(1U<<LS_GP_B));
    assert(unlink(source)==0); /* A disconnected/stale USB state must release. */
    usleep(520000);assert(ls_gp_read(&reader,&readback));
    assert(readback.pads[0].connected&&readback.pads[0].buttons==0&&readback.pads[0].axes[1]==0);
    assert(kill(child,SIGTERM)==0);
    int status;assert(waitpid(child,&status,0)==child&&WIFEXITED(status)&&WEXITSTATUS(status)==0);
    ls_gp_close(&reader);close(usbfd);assert(access(destination,F_OK)==-1);
    munmap((void *)panel,4096);assert(rmdir(folder)==0);
    puts("PASS running broker with fake uinput unavailable: live panel/USB merge, release, USB disconnect, cleanup");
}

int main(void){
    struct ls_gp_state usb,next;
    memset(&usb,0,sizeof(usb));
    assert(sizeof(struct ls_gp_pad)==100);
    assert(sizeof(struct ls_gp_state)==424);
    assert(sizeof(struct input_event)==16);
    snapshot(&next,NULL,1U<<13,0);
    assert(next.magic==LS_GP_MAGIC&&next.abi==1&&next.pads[0].connected);
    assert(next.pads[0].buttons==(1U<<LS_GP_A));
    assert(!next.pads[0].exported&&next.pads[0].axes[0]==0);
    assert(panel_buttons((1U<<10)|(1U<<11)|(1U<<12)|(1U<<3))==
           ((1U<<LS_GP_LB)|(1U<<LS_GP_RB)|(1U<<LS_GP_X)|(1U<<LS_GP_START)));
    usb.pads[0].connected=1;usb.pads[0].instance=37;
    usb.pads[0].buttons=1U<<LS_GP_B;usb.pads[0].axes[0]=-12345;
    usb.pads[3].connected=1;usb.pads[3].instance=99;
    snapshot(&next,&usb,1U<<13,0);
    assert(next.pads[0].buttons==((1U<<LS_GP_A)|(1U<<LS_GP_B)));
    assert(next.pads[0].axes[0]==-12345&&next.pads[0].instance==37);
    assert(next.pads[3].instance==99);
    snapshot(&next,&usb,0,0);
    assert(next.pads[0].buttons==(1U<<LS_GP_B));
    snapshot(&next,NULL,0,0);
    assert(next.pads[0].buttons==0&&next.pads[0].axes[0]==0);
    assert(!next.pads[3].connected);
    snapshot(&next,&usb,1U<<13,1);
    assert(!memcmp(next.pads,usb.pads,sizeof(usb.pads)));
    int fds[2];assert(pipe(fds)==0);
    assert(changes(fds[1],0,1U<<13)==0);
    struct input_event events[4];
    assert(read(fds[0],events,sizeof(events))==(ssize_t)sizeof(events));
    assert(events[0].type==EV_KEY&&events[0].code==BTN_SOUTH&&events[0].value==1);
    assert(events[3].type==EV_SYN);
    assert(changes(fds[1],1U<<13,0)==0);
    assert(read(fds[0],events,sizeof(events))==(ssize_t)sizeof(events));
    assert(events[0].value==0);
    close(fds[0]);close(fds[1]);
    char path[]="/tmp/r36-broker-reader-XXXXXX";
    int file=mkstemp(path);assert(file>=0);
    assert(fchmod(file,0600)==0);
    snapshot(&next,&usb,1U<<12,0);next.sequence=2;
    assert(write(file,&next,sizeof(next))==(ssize_t)sizeof(next));
    assert(setenv("LILSMOKE_GAMEPAD_STATE",path,1)==0);
    struct ls_gp_reader reader=LS_GP_READER_INIT;
    struct ls_gp_state observed;
    assert(ls_gp_read(&reader,&observed)==1);
    assert(observed.pads[0].buttons&1U<<LS_GP_X);
    next.sequence=3;assert(pwrite(file,&next,sizeof(next),0)==(ssize_t)sizeof(next));
    assert(ls_gp_read(&reader,&observed)==0);
    next.sequence=4;next.millis=ls_gp_now()-501;
    assert(pwrite(file,&next,sizeof(next),0)==(ssize_t)sizeof(next));
    assert(ls_gp_read(&reader,&observed)==0);
    ls_gp_close(&reader);close(file);assert(unlink(path)==0);
    puts("PASS panel mapping, four USB slots/axes, release/hotplug, 16-byte evdev, shared-reader ABI, odd/stale rejection");
    integration();
    return 0;
}
