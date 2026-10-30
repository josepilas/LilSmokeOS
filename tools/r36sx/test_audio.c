/* SPDX-License-Identifier: MPL-2.0 */
#define _GNU_SOURCE
#include <assert.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdint.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <dlfcn.h>
static unsigned long observed_request;
static unsigned char observed_config[632];
#ifdef __GLIBC__
static int fake_real_ioctl(int fd,unsigned long request,...){
#else
static int fake_real_ioctl(int fd,int request,...){
#endif
    assert(fd==42);observed_request=(uint32_t)request;
    va_list args;va_start(args,request);
    if(observed_request==0x82780301UL)memcpy(observed_config,va_arg(args,void *),632);
    va_end(args);return 17;
}
static void *fake_dlsym(void *handle,const char *name){(void)handle;assert(!strcmp(name,"ioctl"));return (void *)fake_real_ioctl;}
static int fake_fstat(int fd,struct stat *st){assert(fd==42);memset(st,0,sizeof(*st));st->st_mode=S_IFCHR;st->st_rdev=33;return 0;}
static int fake_stat(const char *path,struct stat *st){assert(!strcmp(path,"/dev/auddec"));return fake_fstat(42,st);}
#define dlsym fake_dlsym
#define fstat(...) fake_fstat(__VA_ARGS__)
#define stat(...) fake_stat(__VA_ARGS__)
#define ioctl r36_test_ioctl
#include "../../board/r36sx-2.7-stock/r36-audio-compat.c"
#undef ioctl
#undef dlsym
#undef fstat
#undef stat
int main(void){
    unsigned char source[624]={0},destination[640];
    memset(destination,0xcd,sizeof(destination));
    source[2]=16;source[3]=2;source[6]=1;source[12]=0x44;source[13]=0xac;
    assert(config_convert(destination,source));
    assert(destination[2]==16&&destination[3]==2&&destination[6]==1);
    assert(destination[12]==0x44&&destination[13]==0xac&&destination[20]==4);
    for(unsigned i=0;i<632;i++)if(i!=2&&i!=3&&i!=6&&i!=12&&i!=13&&i!=20)assert(destination[i]==0);
    assert(destination[632]==0xcd);
    assert(setenv("LILSMOKE_R36_DISPLAY","1",1)==0);
    assert(r36_test_ioctl(42,(request_t)M8_AUDDEC_INIT,source)==17);
    assert(observed_request==R36_AUDDEC_INIT&&!memcmp(observed_config,destination,632));
    assert(r36_test_ioctl(42,(request_t)0x20000304,NULL)==17&&observed_request==0x20000304);
    source[3]=1;assert(!config_convert(destination,source));
    assert(r36_test_ioctl(42,(request_t)M8_AUDDEC_INIT,source)==-1&&errno==EINVAL);
    source[3]=2;source[12]=0;assert(!config_convert(destination,source));
    assert(!config_convert(destination,NULL));
    puts("PASS R36SX init conversion, real ioctl forwarding, request remap, stereo/rate validation, bounded zeroed config");
    return 0;
}
