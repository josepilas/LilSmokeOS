/* SPDX-License-Identifier: MPL-2.0
 * Adapt our bundled M8 PCM backend at its libc ioctl boundary, not by loading
 * a vendor glibc library. R36SX auddec has a distinct 632-byte init ABI.
 * Reference: Witali/r36sx_disasm homebrew/common/native_audio.h, pinned in docs.
 * Hardware playback on this exact 2.7 unit remains unverified. */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>

#define R36_AUDDEC_INIT 0x82780301UL
#define M8_AUDDEC_INIT 0x82700301UL
#ifdef __GLIBC__
typedef unsigned long request_t;
#else
typedef int request_t;
#endif
static int config_convert(unsigned char out[632],const unsigned char *in){
    static const unsigned char codec[]={0,0,1,0};
    static const unsigned char rate[]={0x44,0xac,0,0};
    if(!in||in[2]!=16||in[3]!=2||memcmp(in+4,codec,4)||memcmp(in+12,rate,4))return 0;
    memset(out,0,632);out[2]=16;out[3]=2;
    memcpy(out+4,codec,4);memcpy(out+12,rate,4);
    out[20]=4; /* 16-bit stereo block alignment, stock R36SX PCM helper ABI. */
    return 1;
}
int ioctl(int fd,request_t request,...){
    static int (*real)(int,request_t,...);
    if(!real)real=dlsym(RTLD_NEXT,"ioctl");
    if(!real){errno=ENOSYS;return -1;}
    va_list args;va_start(args,request);void *argument=va_arg(args,void *);va_end(args);
    const char *enabled=getenv("LILSMOKE_R36_DISPLAY");
    if((uint32_t)request==M8_AUDDEC_INIT&&enabled&&!strcmp(enabled,"1")){
        struct stat opened,node;
        if(fstat(fd,&opened)||stat("/dev/auddec",&node)||!S_ISCHR(opened.st_mode)||
           !S_ISCHR(node.st_mode)||opened.st_rdev!=node.st_rdev){errno=ENODEV;return -1;}
        _Alignas(8) unsigned char config[632];
        if(!config_convert(config,argument)){errno=EINVAL;return -1;}
        return real(fd,R36_AUDDEC_INIT,config);
    }
    return real(fd,request,argument);
}
