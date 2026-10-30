/* SPDX-License-Identifier: MPL-2.0 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <linux/kd.h>
#include <mntent.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#include <signal.h>
#include <sys/reboot.h>
#include <sys/vfs.h>
#include <time.h>
#include "m8-power-ui.h"

#define LIMIT 64
struct volume { char path[4096]; int readonly; unsigned long flags; };
static int mount_option(const char *options,const char *wanted) {
    size_t length=strlen(wanted);
    while(*options){
        const char *end=strchr(options,',');size_t size=end?(size_t)(end-options):strlen(options);
        if(size==length&&!memcmp(options,wanted,length))return 1;
        if(!end)break;
        options=end+1;
    }
    return 0;
}
static int write_holder(int pid,const struct volume *volumes,int count) {
    char directory[64];snprintf(directory,sizeof(directory),"/proc/%d/fd",pid);
    DIR *fds=opendir(directory);if(!fds)return 0;
    struct dirent *item;int found=0;
    while(!found&&(item=readdir(fds))) {
        char *end;long number=strtol(item->d_name,&end,10);
        if(*end||number<0)continue;
        char path[96],target[4096];
        snprintf(path,sizeof(path),"/proc/%d/fd/%ld",pid,number);
        ssize_t length=readlink(path,target,sizeof(target)-1);
        if(length<0)continue;
        target[length]=0;
        int on_card=0;
        for(int i=0;i<count;++i) {
            size_t n=strlen(volumes[i].path);
            if(!strncmp(target,volumes[i].path,n)&&(target[n]=='/'||target[n]==0))on_card=1;
        }
        if(!on_card)continue;
        snprintf(path,sizeof(path),"/proc/%d/fdinfo/%ld",pid,number);
        FILE *info=fopen(path,"r");if(!info)continue;
        char line[256];unsigned flags=0;
        while(fgets(line,sizeof(line),info))if(sscanf(line,"flags: %o",&flags)==1)break;
        fclose(info);
        if((flags&O_ACCMODE)!=O_RDONLY)found=1;
    }
    closedir(fds);return found;
}
static void stop_writers(const struct volume *volumes,int count) {
    DIR *proc=opendir("/proc");if(!proc)return;
    struct dirent *entry;int signaled=0;
    while((entry=readdir(proc))) {
        char *end;long pid=strtol(entry->d_name,&end,10);
        if(*end||pid<=1||pid==getpid()||pid==getppid())continue;
        if(write_holder((int)pid,volumes,count)) {
            fprintf(stderr,"M8_POWER stopping SD writer pid=%ld\n",pid);
            if(!kill((pid_t)pid,SIGTERM))signaled=1;
        }
    }
    closedir(proc);
    if(signaled)sleep(2);
}
static int read_mounts(const char *table,struct volume *volumes) {
    FILE *file=setmntent(table,"r");
    if(!file)return -1;
    struct mntent *entry;int count=0;
    while((entry=getmntent(file))) {
        if(strcmp(entry->mnt_type,"vfat")&&strcmp(entry->mnt_type,"exfat"))continue;
        if(strncmp(entry->mnt_fsname,"/dev/mmcblk0p",sizeof("/dev/mmcblk0p")-1))continue;
        if(count==LIMIT||strlen(entry->mnt_dir)>=sizeof(volumes[0].path)) {endmntent(file);return -1;}
        char *out=volumes[count].path;
        const char *in=entry->mnt_dir;
        while(*in) {
            if(in[0]=='\\'&&in[1]>='0'&&in[1]<='7'&&in[2]>='0'&&in[2]<='7'&&in[3]>='0'&&in[3]<='7') {
                *out++=(char)((in[1]-'0')*64+(in[2]-'0')*8+in[3]-'0');in+=4;
            } else *out++=*in++;
        }
        *out=0;
        if(volumes[count].path[0]!='/'||!strcmp(volumes[count].path,"/")) {endmntent(file);return -1;}
        /* musl hasmntopt() also matches substrings such as errors=remount-ro. */
        volumes[count].readonly=mount_option(entry->mnt_opts,"ro");
        volumes[count].flags=MS_RDONLY|MS_REMOUNT;
        if(mount_option(entry->mnt_opts,"nodev"))volumes[count].flags|=MS_NODEV;
        if(mount_option(entry->mnt_opts,"nosuid"))volumes[count].flags|=MS_NOSUID;
        if(mount_option(entry->mnt_opts,"noexec"))volumes[count].flags|=MS_NOEXEC;
        ++count;
    }
    endmntent(file);return count;
}
static int labels(void) {
    const char *devices[]={"/dev/mmcblk0p1","/dev/mmcblk0p2"};
    const char *expected[]={"LILOS      ","LILROMS    "};
    for(unsigned i=0;i<2;++i) {
        int fd=open(devices[i],O_RDONLY|O_CLOEXEC);unsigned char sector[512];
        if(fd<0)return -1;
        struct stat st;int valid=!fstat(fd,&st)&&S_ISBLK(st.st_mode)&&pread(fd,sector,sizeof(sector),0)==sizeof(sector);
        close(fd);
        if(!valid||memcmp(sector+82,"FAT32   ",8)||memcmp(sector+71,expected[i],11))return -1;
    }
    return 0;
}
static int flush_devices(void) {
    const char *devices[]={"/dev/mmcblk0p1","/dev/mmcblk0p2","/dev/mmcblk0"};
    for(unsigned i=0;i<3;++i) {
        int fd=open(devices[i],O_RDONLY|O_CLOEXEC);
        if(fd<0)return -1;
        int result=fsync(fd);int saved=errno;close(fd);errno=saved;
        if(result)return -1;
    }
    return 0;
}
static uint32_t little32(const unsigned char *p) {
    return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);
}
static int fat_clean_flags(int fd,const unsigned char sector[512]) {
    unsigned reserved=(unsigned)sector[14]|((unsigned)sector[15]<<8);
    unsigned bytes=(unsigned)sector[11]|((unsigned)sector[12]<<8),fats=sector[16];
    uint32_t length=little32(sector+36),total=little32(sector+32);
    if(bytes!=512||!reserved||!length||!fats||fats>2||total<=reserved+(uint64_t)fats*length)return -1;
    if(sector[65]&3){fprintf(stderr,"M8_POWER FAT boot state=%02x\n",sector[65]);return 0;}
    for(unsigned i=0;i<fats;++i){
        unsigned char entry[4];off_t offset=((off_t)reserved+(off_t)i*length)*512+4;
        if(pread(fd,entry,4,offset)!=4)return -1;
        if((little32(entry)&0x0c000000U)!=0x0c000000U){fprintf(stderr,"M8_POWER FAT mirror=%u reserved_entry=%08x\n",i,little32(entry));return 0;}
    }
    return 1;
}
static int clean_devices(void) {
    const char *devices[]={"/dev/mmcblk0p1","/dev/mmcblk0p2"};int result=1;
    for(unsigned i=0;i<2;++i){
        unsigned char sector[512];int fd=open(devices[i],O_RDONLY|O_CLOEXEC);
        if(fd<0)return -1;
        int clean=pread(fd,sector,512,0)==512?fat_clean_flags(fd,sector):-1;close(fd);
        if(clean<0)return -1;
        if(!clean){fprintf(stderr,"M8_POWER filesystem_check_required device=%s\n",devices[i]);result=0;}
    }
    return result;
}
static int terminal_board(void) {
    struct statfs fs;
    if(statfs("/proc/self/exe",&fs))return 0;
    if(fs.f_type!=0x01021994&&fs.f_type!=0x858458f6){fprintf(stderr,"M8_POWER executable is not RAM-backed: fs=%lx\n",(unsigned long)fs.f_type);return 0;}
    int fd=open("/dev/fb0",O_RDONLY|O_CLOEXEC);if(fd<0)return 0;
    struct fb_fix_screeninfo info;int valid=!ioctl(fd,FBIOGET_FSCREENINFO,&info);close(fd);
#ifdef M8_POWER_QA
    valid=valid&&(!memcmp(info.id,"hcfb",4)||!memcmp(info.id,"CL Picasso4",11));
#else
    valid=valid&&!memcmp(info.id,"hcfb",4);
#endif
    if(!valid)fprintf(stderr,"M8_POWER unsupported framebuffer: %.16s\n",info.id);
    FILE *marker=fopen("/tmp/lilsmoke-m8-shutdown","r");char text[32];
    valid=valid&&marker&&fgets(text,sizeof(text),marker)&&!strcmp(text,"poweroff\n");
    if(marker)fclose(marker);
    return valid;
}
static int userspace_process(int pid) {
    char path[64],byte;snprintf(path,sizeof(path),"/proc/%d/cmdline",pid);
    int fd=open(path,O_RDONLY|O_CLOEXEC);if(fd<0)return 0;
    int user=read(fd,&byte,1)==1;close(fd);return user;
}
static void stop_userspace(int sig) {
    DIR *proc=opendir("/proc");if(!proc)return;
    struct dirent *entry;
    while((entry=readdir(proc))){char *end;long pid=strtol(entry->d_name,&end,10);
        if(*end||pid<=1||pid==getpid()||pid>0x7fffffffL)continue;
        if(userspace_process((int)pid))kill((pid_t)pid,sig);
    }
    closedir(proc);
}
static void hold_success(void) {
    struct timespec now,deadline;
    if(clock_gettime(CLOCK_MONOTONIC,&deadline)){sleep(10);return;}
    deadline.tv_sec+=10;
    for(;;){
        if(clock_gettime(CLOCK_MONOTONIC,&now))continue;
        if(now.tv_sec>deadline.tv_sec||(now.tv_sec==deadline.tv_sec&&now.tv_nsec>=deadline.tv_nsec))break;
        struct timespec wait={deadline.tv_sec-now.tv_sec,deadline.tv_nsec-now.tv_nsec};
        if(wait.tv_nsec<0){--wait.tv_sec;wait.tv_nsec+=1000000000L;}
        nanosleep(&wait,NULL);
    }
}
static const char *font[] = {
    "01110100011000111111100011000110001", "11110100011000111110100011000111110",
    "01111100001000010000100001000001111", "11110100011000110001100011000111110",
    "11111100001000011110100001000011111", "11111100001000011110100001000010000",
    "01111100001000010111100011000101111", "10001100011000111111100011000110001",
    "11111001000010000100001000010011111", "00111000100001000010100101001001100",
    "10001100101010011000101001001010001", "10000100001000010000100001000011111",
    "10001110111010110101100011000110001", "10001110011010110011100011000110001",
    "01110100011000110001100011000101110", "11110100011000111110100001000010000",
    "01110100011000110001101011001001101", "11110100011000111110101001001010001",
    "01111100001000001110000010000111110", "11111001000010000100001000010000100",
    "10001100011000110001100011000101110", "10001100011000110001100010101000100",
    "10001100011000110101101011101110001", "10001100010101000100010101000110001",
    "10001100010101000100001000010000100", "11111000010001000100010001000011111"
};
static uint32_t color(struct fb_var_screeninfo *v,unsigned r,unsigned g,unsigned b) {
    return ((r>>(8-v->red.length))<<v->red.offset)|((g>>(8-v->green.length))<<v->green.offset)|
           ((b>>(8-v->blue.length))<<v->blue.offset)|(v->transp.length?((1U<<v->transp.length)-1)<<v->transp.offset:0);
}
static void status_screen(int ready) {
    int fd=open("/dev/fb0",O_RDWR|O_CLOEXEC);
    if(fd<0)return;
    struct fb_fix_screeninfo f;struct fb_var_screeninfo v;
    if(ioctl(fd,FBIOGET_FSCREENINFO,&f)||ioctl(fd,FBIOGET_VSCREENINFO,&v)||
       strncmp(f.id,"hcfb",4)||v.bits_per_pixel!=32||v.xres<320||v.xres>1920||v.yres<240||v.yres>1080||
       !v.red.length||v.red.length>8||!v.green.length||v.green.length>8||!v.blue.length||v.blue.length>8||v.transp.length>8||
       (uint64_t)(v.yoffset+v.yres)*f.line_length>f.smem_len||
       (uint64_t)(v.xoffset+v.xres)*4>f.line_length||
       v.red.offset+v.red.length>32||v.green.offset+v.green.length>32||v.blue.offset+v.blue.length>32||
       v.transp.offset+v.transp.length>32) {close(fd);return;}
    unsigned char *memory=mmap(NULL,f.smem_len,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0);
    if(memory==MAP_FAILED) {close(fd);return;}
    uint32_t bg=color(&v,18,23,26),fg=color(&v,ready?111:255,ready?218:185,ready?151:80);
    for(unsigned y=0;y<v.yres;++y) {
        uint32_t *row=(uint32_t *)(memory+(y+v.yoffset)*f.line_length)+v.xoffset;
        for(unsigned x=0;x<v.xres;++x)row[x]=bg;
    }
    const char *text=ready?"SAFE TO UNPLUG":"STORAGE BUSY WAIT";
    int scale=v.xres>=1000?6:3;
    int x=((int)v.xres-(int)strlen(text)*6*scale)/2,y=((int)v.yres-7*scale)/2;
    for(unsigned i=0;text[i];++i) {
        if(text[i]<'A'||text[i]>'Z')continue;
        for(int yy=0;yy<7;++yy)for(int xx=0;xx<5;++xx)if(font[text[i]-'A'][yy*5+xx]=='1')
            for(int dy=0;dy<scale;++dy)for(int dx=0;dx<scale;++dx) {
                int px=x+(int)i*6*scale+xx*scale+dx,py=y+yy*scale+dy;
                if(px>=0&&py>=0&&px<(int)v.xres&&py<(int)v.yres)
                    ((uint32_t *)(memory+(py+v.yoffset)*f.line_length))[px+v.xoffset]=fg;
            }
    }
    msync(memory,f.smem_len,MS_SYNC);munmap(memory,f.smem_len);close(fd);
}
int main(int argc,char **argv) {
    struct volume volumes[LIMIT];
    if(argc==2&&!strcmp(argv[1],"--self-check")) {
#ifdef M8_POWER_QA
        puts("M8_POWER_GUARD QA_ONLY");
#else
        puts("M8_POWER_GUARD version=2");
#endif
        return 0;
    }
    if(argc==3&&!strcmp(argv[1],"--check-table")) {
        int count=read_mounts(argv[2],volumes);
        if(count<0)return 75;
        for(int i=0;i<count;++i)if(!volumes[i].readonly)return 75;
        printf("M8_POWER_TEST_READONLY mounts=%d\n",count);return 0;
    }
    int terminal=argc==2&&!strcmp(argv[1],"--shutdown");
    if(argc!=2||(!terminal&&strcmp(argv[1],"--prepare"))||geteuid()!=0)return 75;
    if(labels()){fputs("M8_POWER refused: expected LILOS/LILROMS block devices unavailable\n",stderr);return 75;}
    if(terminal){
        if(!terminal_board()){fputs("M8_POWER refused: requires RAM executable, supported framebuffer and shutdown marker\n",stderr);return 75;}
        signal(SIGTERM,SIG_IGN);signal(SIGHUP,SIG_IGN);signal(SIGINT,SIG_IGN);
        /* Prevent the kernel text cursor from repainting the final black frame. */
        int console=open("/dev/tty0",O_RDWR|O_CLOEXEC);
        if(console>=0){ioctl(console,KDSETMODE,KD_GRAPHICS);close(console);}
        m8_power_show(M8_POWER_BUSY);
        if(kill(1,SIGTSTP)){perror("M8_POWER cannot pause init respawn");return 75;}
        stop_userspace(SIGTERM);sleep(2);stop_userspace(SIGKILL);sleep(1);
    }
retry:
    sync();
    int count=read_mounts("/proc/mounts",volumes);
    if(count<0){if(terminal){sleep(5);goto retry;}return 75;}
    if(terminal)fprintf(stderr,"M8_POWER card_mounts=%d\n",count);
    stop_writers(volumes,count);
    sync();
    /* Process aliases before their parent mount, preserving mount flags. */
    for(int i=0;i<count;++i)for(int j=i+1;j<count;++j)
        if(strlen(volumes[j].path)>strlen(volumes[i].path)) {struct volume t=volumes[i];volumes[i]=volumes[j];volumes[j]=t;}
    for(int i=0;i<count;++i)if(!volumes[i].readonly) {
        if(umount(volumes[i].path)&&mount(NULL,volumes[i].path,NULL,volumes[i].flags,NULL)&&
           mount(NULL,volumes[i].path,NULL,volumes[i].flags|MS_BIND,NULL)) {
            perror("M8_POWER release/readonly");status_screen(0);
            if(terminal){m8_power_show(M8_POWER_BUSY);sleep(5);goto retry;}return 75;
        }
    }
    sync();
    count=read_mounts("/proc/mounts",volumes);
    if(count<0){if(terminal){sleep(5);goto retry;}return 75;}
    for(int i=0;i<count;++i)if(!volumes[i].readonly) {status_screen(0);if(terminal){sleep(5);goto retry;}return 75;}
    if(flush_devices()) {perror("M8_POWER device flush");status_screen(0);if(terminal){sleep(5);goto retry;}return 75;}
    if(terminal){
        int clean=clean_devices();
        if(clean<0){perror("M8_POWER clean-flag read");sleep(5);goto retry;}
        printf("M8_POWER_READY storage_unmounted_or_readonly=1 devices_fsynced=1 filesystem_clean=%d\n",clean);
        enum m8_power_screen state=clean?M8_POWER_SUCCESS:M8_POWER_CHECK_CARD;
        if(!m8_power_show(state))fputs("M8_POWER display unavailable\n",stderr);
        puts(m8_power_message(state));fflush(stdout);
        hold_success();
        m8_power_show(M8_POWER_BLACK);
        puts("M8_POWER_BLACK hold_seconds=10");fflush(stdout);
        sync();reboot(RB_POWER_OFF);
        perror("M8_POWER kernel poweroff returned");
        for(;;)sleep(3600);
    }
    puts("M8_POWER_READY storage_unmounted_or_readonly=1 devices_fsynced=1");
    return 0;
}
