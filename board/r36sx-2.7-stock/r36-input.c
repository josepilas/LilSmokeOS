/* SPDX-License-Identifier: MPL-2.0
 * Transport the vendor panel without changing cubevol's SysV IPC ownership. */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <linux/uinput.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <unistd.h>
#include "../../package/lilsmoke-od-sdl/src/input-wire.h"
#include "../../package/lilsmoke-ui/src/src/gamepad-state.h"

static volatile sig_atomic_t stop;
static void quit(int s){(void)s;stop=1;}
struct key {unsigned bit,code;};
static const struct key keys[]={{0,BTN_SELECT},{3,BTN_START},{10,BTN_TL},{11,BTN_TR},
                               {12,BTN_WEST},{13,BTN_SOUTH},{14,BTN_EAST},{15,BTN_NORTH}};
static int changes(int fd,uint32_t before,uint32_t after);
static uint32_t panel_buttons(uint32_t raw){
    static const unsigned bits[]={13,14,12,15,0,3,10,11,4,6,7,5};
    static const unsigned buttons[]={LS_GP_A,LS_GP_B,LS_GP_X,LS_GP_Y,LS_GP_BACK,LS_GP_START,
                                    LS_GP_LB,LS_GP_RB,LS_GP_UP,LS_GP_DOWN,LS_GP_LEFT,LS_GP_RIGHT};
    uint32_t result=0;
    for(unsigned i=0;i<sizeof(bits)/sizeof(bits[0]);i++)
        if(raw&(1U<<bits[i]))result|=1U<<buttons[i];
    return result;
}
static void snapshot(struct ls_gp_state *out,const struct ls_gp_state *usb,uint32_t raw,int native){
    if(usb)memcpy(out,usb,sizeof(*out));else memset(out,0,sizeof(*out));
    out->magic=LS_GP_MAGIC;out->abi=1;out->pid=(uint32_t)getpid();out->millis=ls_gp_now();
    if(!native){
        struct ls_gp_pad *p=&out->pads[0];
        /* Built-in controls and the first USB pad share player one, retaining
         * all four USB slots and their analog axes. Never re-export USB pads. */
        if(!p->connected||p->exported){
            memset(p,0,sizeof(*p));p->connected=1;p->instance=0x52333601;
            p->vendor=0x1209;p->product=0x3650;p->version=1;
            strcpy(p->name,"LilSmoke R36SX Panel");
        }
        p->buttons|=panel_buttons(raw);
    }
}
static int open_uinput(void){
    int fd=open("/dev/uinput",O_WRONLY|O_CLOEXEC);
    if(fd<0)fd=open("/dev/input/uinput",O_WRONLY|O_CLOEXEC);
    if(fd<0)return -1;
    if(ioctl(fd,UI_SET_EVBIT,EV_KEY)||ioctl(fd,UI_SET_EVBIT,EV_ABS)||
       ioctl(fd,UI_SET_ABSBIT,ABS_HAT0X)||ioctl(fd,UI_SET_ABSBIT,ABS_HAT0Y))goto failed;
    for(unsigned i=0;i<sizeof(keys)/sizeof(keys[0]);i++)if(ioctl(fd,UI_SET_KEYBIT,keys[i].code))goto failed;
    struct uinput_user_dev d;memset(&d,0,sizeof(d));
    strcpy(d.name,"LilSmoke R36SX Panel");d.id.bustype=BUS_VIRTUAL;d.id.vendor=0x1209;d.id.product=0x3650;d.id.version=1;
    d.absmin[ABS_HAT0X]=d.absmin[ABS_HAT0Y]=-1;d.absmax[ABS_HAT0X]=d.absmax[ABS_HAT0Y]=1;
    if(write(fd,&d,sizeof(d))!=(ssize_t)sizeof(d)||ioctl(fd,UI_DEV_CREATE))goto failed;
    return fd;
failed:
    close(fd);return -1;
}
static int broker(const volatile uint32_t *panel,const char *source,const char *destination){
    if(!strcmp(source,destination)||setenv("LILSMOKE_GAMEPAD_STATE",source,1))return 64;
    int fd=open(destination,O_RDWR|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);
    if(fd<0){perror("R36 broker create");return 1;}
    if(ftruncate(fd,sizeof(struct ls_gp_state))){close(fd);unlink(destination);return 1;}
    struct ls_gp_state *map=mmap(NULL,sizeof(*map),PROT_READ|PROT_WRITE,MAP_SHARED,fd,0);
    if(map==MAP_FAILED){close(fd);unlink(destination);return 1;}
    int native=open_uinput();
    fprintf(stderr,"R36_PANEL_TRANSPORT=%s\n",native<0?"broker-abi1":"uinput+broker-abi1");
    struct ls_gp_reader reader=LS_GP_READER_INIT;
    uint32_t old=0,sequence=0;
    while(!stop){
        struct ls_gp_state usb,next;
        uint32_t raw=*panel&0xffffU;
        if(native>=0&&changes(native,old,raw)){
            (void)ioctl(native,UI_DEV_DESTROY);close(native);native=-1;
            fprintf(stderr,"R36_PANEL_TRANSPORT=broker-abi1 (uinput disconnected)\n");
        }
        old=raw;
        snapshot(&next,ls_gp_read(&reader,&usb)?&usb:NULL,raw,native>=0);
        __atomic_store_n(&map->sequence,sequence+1,__ATOMIC_RELEASE);
        __atomic_thread_fence(__ATOMIC_SEQ_CST);
        next.sequence=sequence+1;
        memcpy(map,&next,sizeof(next));
        sequence+=2;
        __atomic_store_n(&map->sequence,sequence,__ATOMIC_RELEASE);
        usleep(8000);
    }
    if(native>=0){(void)changes(native,old,0);(void)ioctl(native,UI_DEV_DESTROY);close(native);}
    ls_gp_close(&reader);munmap(map,sizeof(*map));
    struct stat opened,current;
    if(!fstat(fd,&opened)&&!lstat(destination,&current)&&opened.st_dev==current.st_dev&&opened.st_ino==current.st_ino)
        unlink(destination);
    close(fd);return 0;
}
static int emit(int fd,unsigned type,unsigned code,int value){
    struct input_event e;memset(&e,0,sizeof(e));e.type=type;e.code=code;e.value=value;
    return write(fd,&e,sizeof(e))==(ssize_t)sizeof(e)?0:-1;
}
static int changes(int fd,uint32_t before,uint32_t after){
    if(before==after)return 0;
    for(unsigned i=0;i<sizeof(keys)/sizeof(keys[0]);i++){
        unsigned bit=1U<<keys[i].bit;
        if((before^after)&bit)if(emit(fd,EV_KEY,keys[i].code,!!(after&bit)))return -1;
    }
    int x=!!(after&(1U<<5))-!!(after&(1U<<7));
    int y=!!(after&(1U<<6))-!!(after&(1U<<4));
    if(emit(fd,EV_ABS,ABS_HAT0X,x)||emit(fd,EV_ABS,ABS_HAT0Y,y)||emit(fd,EV_SYN,SYN_REPORT,0))return -1;
    return 0;
}
int main(int argc,char **argv){
    if(argc==2&&!strcmp(argv[1],"--self-check")){puts("R36_PANEL_INPUT version=2 ABI=1");return 0;}
    if(argc==2&&!strcmp(argv[1],"--test-events")){
        uint32_t old=0,now;while(scanf("%x",&now)==1){if(changes(1,old,now))return 1;old=now;}return 0;
    }
    key_t key=ftok("/tmp/joy_key",'a');
    if(argc==2&&!strcmp(argv[1],"--key")){
        if(key==(key_t)-1)return 1;
        printf("%ld\n",(long)key);return 0;
    }
    if(argc==3&&!strcmp(argv[1],"--check-key")){
        char *end;long expected=strtol(argv[2],&end,10);
        if(*end||key==(key_t)-1||expected!=(long)key)return 1;
        printf("R36_PANEL_KEY=%ld\n",(long)key);return 0;
    }
    int use_broker=argc==4&&!strcmp(argv[1],"--broker");
    if((argc!=1&&!use_broker)||key==(key_t)-1)return 64;
    int id=shmget(key,sizeof(uint32_t),0666);
    if(id<0){perror("R36 panel shmget");return 1;}
    const volatile uint32_t *state=shmat(id,NULL,SHM_RDONLY);
    if(state==(void *)-1){perror("R36 panel shmat");return 1;}
    signal(SIGTERM,quit);signal(SIGINT,quit);signal(SIGHUP,quit);
    if(use_broker){int result=broker(state,argv[2],argv[3]);shmdt((const void *)state);return result;}
    int fd=open_uinput();
    if(fd<0){perror("R36 panel uinput unavailable");shmdt((const void *)state);return 69;}
    uint32_t old=0;
    while(!stop){uint32_t now=*state&0xffffU;if(changes(fd,old,now))break;old=now;usleep(8000);}
    (void)changes(fd,old,0);(void)ioctl(fd,UI_DEV_DESTROY);close(fd);shmdt((const void *)state);return 0;
}
