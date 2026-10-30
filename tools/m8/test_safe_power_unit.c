/* All destructive calls are replaced below. Never operates on real devices. */
#define _GNU_SOURCE
#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <linux/kd.h>
#include <mntent.h>
#include <setjmp.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <sys/reboot.h>
#include <sys/stat.h>
#include <sys/vfs.h>
#include <time.h>
#include <unistd.h>
#include "../../board/segam-m8-v6-stock/m8-power-ui.h"

static FILE *fat;
static jmp_buf finished;
static int released, paused, terminated, killed, flushed, retry_flush, warning, proc_index;
static int screen_count, screens[8];
static time_t now, success_at, black_at;
static int mock_open(const char *path,int flags,...) {
    assert((flags&O_ACCMODE)==O_RDONLY || !strcmp(path,"/dev/fb0") || !strcmp(path,"/dev/tty0"));
    if(!strcmp(path,"/dev/tty0"))return dup(fileno(fat));
    if(!strncmp(path,"/dev/mmcblk0",strlen("/dev/mmcblk0"))||!strcmp(path,"/dev/fb0"))return dup(fileno(fat));
    assert(!strncmp(path,"/proc/",6));assert(lseek(fileno(fat),0,SEEK_SET)==0);return dup(fileno(fat));
}
static int mock_fstat(int fd,struct stat *s) {(void)fd;memset(s,0,sizeof(*s));s->st_mode=S_IFBLK;return 0;}
static ssize_t mock_pread(int fd,void *buf,size_t n,off_t offset) {
    ssize_t result=pread(fd,buf,n,offset);
    if(n==512&&offset==0) {
        /* Both labels are accepted here; label rejection is tested separately. */
        static unsigned partition;memcpy((char *)buf+71,partition++%2?"LILROMS    ":"LILOS      ",11);
    }
    return result;
}
static int mock_statfs(const char *path,struct statfs *s) {assert(!strcmp(path,"/proc/self/exe"));memset(s,0,sizeof(*s));s->f_type=0x01021994;return 0;}
static int mock_ioctl(int fd,unsigned long request,...) {
    (void)fd;va_list ap;va_start(ap,request);
    if(request==KDSETMODE){assert(va_arg(ap,int)==KD_GRAPHICS);va_end(ap);return 0;}
    if(request==FBIOGET_FSCREENINFO){struct fb_fix_screeninfo *s=va_arg(ap,void *);memset(s,0,sizeof(*s));memcpy(s->id,"hcfb",4);va_end(ap);return 0;}
    va_end(ap);return -1;
}
static FILE *mock_fopen(const char *path,const char *mode) {
    if(!strcmp(path,"/tmp/lilsmoke-m8-shutdown")){FILE *f=tmpfile();assert(f);fputs("poweroff\n",f);rewind(f);return f;}
    return fopen(path,mode);
}
static FILE *mock_setmntent(const char *path,const char *mode) {
    assert(!strcmp(path,"/proc/mounts"));(void)mode;FILE *f=tmpfile();assert(f);
    if(!released)fputs("/dev/mmcblk0p1 /lib vfat rw 0 0\n/dev/mmcblk0p1 /mnt/sdcard vfat rw 0 0\n/dev/mmcblk0p2 /storage vfat rw 0 0\n",f);
    rewind(f);return f;
}
static DIR *mock_opendir(const char *path){if(strcmp(path,"/proc"))return NULL;proc_index=0;return (DIR *)(uintptr_t)1;}
static struct dirent *mock_readdir(DIR *d) {
    assert(d==(DIR *)(uintptr_t)1);static struct dirent e;
    if(proc_index==3)return NULL;
    memset(&e,0,sizeof(e));snprintf(e.d_name,sizeof(e.d_name),"%d",(int[]){1,50,51}[proc_index++]);return &e;
}
static int mock_closedir(DIR *d){assert(d==(DIR *)(uintptr_t)1);return 0;}
static int mock_kill(pid_t pid,int sig) {
    if(pid==1){assert(sig==SIGTSTP);paused=1;return 0;}
    assert(paused&&(pid==50||pid==51));
    if(sig==SIGTERM)++terminated;
    else {assert(sig==SIGKILL&&terminated==2);++killed;}
    return 0;
}
static int mock_umount(const char *path) {
    assert(paused&&terminated==2&&killed==2);assert(strcmp(path,"/lib")||released);
    ++released;return 0;
}
static int mock_mount(const char *a,const char *b,const char *c,unsigned long flags,const void *d) {
    (void)a;(void)b;(void)c;(void)flags;(void)d;assert(0);return -1;
}
static int mock_fsync(int fd) {
    (void)fd;assert(released==3);
    if(retry_flush){retry_flush=0;errno=EIO;return -1;}
    ++flushed;return 0;
}
static void mock_sync(void){}
static unsigned mock_sleep(unsigned n){now+=n;return 0;}
static int mock_clock(clockid_t id,struct timespec *s){assert(id==CLOCK_MONOTONIC);s->tv_sec=now;s->tv_nsec=0;return 0;}
static int mock_nanosleep(const struct timespec *s,struct timespec *remaining) {
    (void)remaining;assert(s->tv_sec==10&&s->tv_nsec==0);now+=s->tv_sec;return 0;
}
static int mock_reboot(int command){assert(command==RB_POWER_OFF&&black_at-success_at==10&&flushed==3);longjmp(finished,1);return -1;}
static uid_t mock_geteuid(void){return 0;}

/* Only the implementation under test sees these replacements. */
#define open mock_open
#define fstat mock_fstat
#define pread mock_pread
#define statfs(...) mock_statfs(__VA_ARGS__)
#define ioctl mock_ioctl
#define fopen mock_fopen
#define setmntent mock_setmntent
#define opendir mock_opendir
#define readdir mock_readdir
#define closedir mock_closedir
#define kill mock_kill
#define umount mock_umount
#define mount mock_mount
#define fsync mock_fsync
#define sync mock_sync
#define sleep mock_sleep
#define clock_gettime mock_clock
#define nanosleep mock_nanosleep
#define reboot mock_reboot
#define geteuid mock_geteuid
#define main guard_main
#include "../../board/segam-m8-v6-stock/m8-safe-power.c"
#undef main
#undef open
#undef fstat
#undef pread
#undef statfs
#undef ioctl
#undef fopen
#undef setmntent
#undef opendir
#undef readdir
#undef closedir
#undef kill
#undef umount
#undef mount
#undef fsync
#undef sync
#undef sleep
#undef clock_gettime
#undef nanosleep
#undef reboot
#undef geteuid

int m8_power_show(enum m8_power_screen state) {
    assert(screen_count<8);screens[screen_count++]=state;
    if(state==M8_POWER_SUCCESS||state==M8_POWER_CHECK_CARD){assert(released==3&&flushed==3);success_at=now;}
    if(state==M8_POWER_BLACK)black_at=now;
    return 1;
}
const char *m8_power_message(enum m8_power_screen state) {
    return state==M8_POWER_SUCCESS?"Power off operation succeeded":"Storage closed but SD needs repair";
}
static void put32(unsigned char *p,uint32_t v){for(unsigned i=0;i<4;++i)p[i]=(unsigned char)(v>>(8*i));}
static void fixture(int dirty) {
    unsigned char data[4096]={0};data[11]=0;data[12]=2;data[14]=1;data[16]=2;
    put32(data+36,1);put32(data+32,100);memcpy(data+82,"FAT32   ",8);data[65]=dirty?1:0;
    put32(data+516,0x0fffffff);put32(data+1028,0x0fffffff);
    rewind(fat);assert(fwrite(data,1,sizeof(data),fat)==sizeof(data));fflush(fat);
}
int main(void) {
    fat=tmpfile();assert(fat);fixture(0);
    unsigned char sector[512];assert(pread(fileno(fat),sector,512,0)==512);
    assert(fat_clean_flags(fileno(fat),sector)==1);
    sector[65]=1;assert(fat_clean_flags(fileno(fat),sector)==0);sector[65]=2;assert(fat_clean_flags(fileno(fat),sector)==0);sector[65]=0;
    sector[12]=0;assert(fat_clean_flags(fileno(fat),sector)==-1);sector[12]=2;
    unsigned char bad[4]={0xff,0xff,0xff,0x03};assert(pwrite(fileno(fat),bad,4,1028)==4);
    assert(fat_clean_flags(fileno(fat),sector)==0);
    for(warning=0;warning<2;++warning){
        fixture(warning);released=paused=terminated=killed=flushed=screen_count=0;now=0;retry_flush=warning;
        if(!setjmp(finished)){char *args[]={"guard","--shutdown",NULL};guard_main(2,args);assert(0);}
        assert(screens[0]==M8_POWER_BUSY);
        assert(screens[1]==(warning?M8_POWER_CHECK_CARD:M8_POWER_SUCCESS));assert(screens[2]==M8_POWER_BLACK);
        assert(black_at-success_at==10);
    }
    fclose(fat);puts("PASS mocked terminal: init pause, TERM/KILL, unmount, fsync retry, FAT flags, exact 10s, black, kernel halt");return 0;
}
