/* SPDX-License-Identifier: MPL-2.0
 * Bounded, preallocated journal, usable before libc or the UI is initialized. */
#ifndef R36_BOOTLOG_H
#define R36_BOOTLOG_H
#include <stdint.h>
#include <fcntl.h>
#include <sys/syscall.h>
#ifndef R36_JOURNAL_PATH
#define R36_JOURNAL_PATH "/mnt/sdcard/lilsmoke/boot-journal.bin"
#endif
#define R36_RECORD_SIZE 512
#define R36_RECORD_COUNT 32

#ifdef R36_BOOTLOG_HOST
#include <unistd.h>
#include <errno.h>
static long r36_raw(long n,long a,long b,long c) {
    long r=syscall(n,a,b,c); return r<0?-errno:r;
}
#else
static long r36_raw(long n,long p0,long p1,long p2) {
    register long v0 __asm__("$2")=n;
    register long a0 __asm__("$4")=p0;
    register long a1 __asm__("$5")=p1;
    register long a2 __asm__("$6")=p2;
    register long a3 __asm__("$7");
    /* Linux o32 may overwrite temporaries even for a three-argument syscall.
     * qemu-user preserves more than a real kernel and cannot qualify this ABI. */
    __asm__ volatile("syscall" : "+r"(v0),"=r"(a3) : "r"(a0),"r"(a1),"r"(a2)
        : "$1","$3","$8","$9","$10","$11","$12","$13","$14","$15",
          "$24","$25","hi","lo","memory");
    return a3?-v0:v0;
}
#endif
static uint32_t r36_get32(const unsigned char *p) {
    return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
static void r36_put32(unsigned char *p,uint32_t v) {
    for(unsigned i=0;i<4;i++) p[i]=(unsigned char)(v>>(8*i));
}
static uint32_t r36_crc(const unsigned char *p,unsigned n) {
    uint32_t c=~0u;
    for(unsigned i=0;i<n;i++) {
        c^=p[i];
        for(unsigned b=0;b<8;b++) c=(c>>1)^(0xedb88320u&(0u-(c&1u)));
    }
    return ~c;
}
static int r36_valid(const unsigned char *p) {
    static const char magic[]="LSBJ0001";
    for(unsigned i=0;i<8;i++) if(p[i]!=(unsigned char)magic[i]) return 0;
    return r36_get32(p+8)!=0 && r36_crc(p,508)==r36_get32(p+508);
}
static uint32_t r36_uptime_ms(void) {
    char p[48]; uint32_t seconds=0, fraction=0; unsigned i=0,digits=0;
    long fd=r36_raw(SYS_open,(long)"/proc/uptime",O_RDONLY|O_CLOEXEC,0);
    if(fd<0) return 0;
    long n=r36_raw(SYS_read,fd,(long)p,sizeof(p));
    (void)r36_raw(SYS_close,fd,0,0);
    while(i<(unsigned)(n>0?n:0) && p[i]>='0' && p[i]<='9') {
        if(seconds>4294966u) return UINT32_MAX;
        seconds=seconds*10u+(unsigned)(p[i++]-'0');
    }
    if(i<(unsigned)(n>0?n:0) && p[i++]=='.') {
        while(i<(unsigned)n && digits<3 && p[i]>='0' && p[i]<='9') {
            fraction=fraction*10u+(unsigned)(p[i++]-'0'); digits++;
        }
    }
    while(digits++<3) fraction*=10;
    return seconds>4294966u?UINT32_MAX:seconds*1000u+fraction;
}
static int r36_journal_write(const char *path,const char *event) {
    unsigned char record[R36_RECORD_SIZE]; uint32_t seq=0; long result=-1;
    /* Never create, truncate, follow a link, or extend the file on the card. */
    long fd=r36_raw(SYS_open,(long)path,O_RDWR|O_NOFOLLOW|O_CLOEXEC,0);
    if(fd<0) return (int)fd;
    if(r36_raw(SYS_flock,fd,2|4,0)<0) goto done; /* LOCK_EX | LOCK_NB */
    if(r36_raw(SYS_lseek,fd,0,2)!=R36_RECORD_SIZE*R36_RECORD_COUNT) goto done;
    if(r36_raw(SYS_lseek,fd,0,0)!=0) goto done;
    for(unsigned i=0;i<R36_RECORD_COUNT;i++) {
        if(r36_raw(SYS_read,fd,(long)record,sizeof(record))!=(long)sizeof(record)) goto done;
        if(r36_valid(record) && r36_get32(record+8)>seq) seq=r36_get32(record+8);
    }
    if(seq==UINT32_MAX) goto done;
    for(unsigned i=0;i<sizeof(record);i++) record[i]=0;
    static const char magic[]="LSBJ0001";
    for(unsigned i=0;i<8;i++) record[i]=(unsigned char)magic[i];
    r36_put32(record+8,seq+1);
    r36_put32(record+12,r36_uptime_ms());
    r36_put32(record+16,(uint32_t)r36_raw(SYS_getpid,0,0,0));
    unsigned i=0;
    while(i<479 && event[i]) {
        unsigned char c=(unsigned char)event[i];
        record[24+i]=(c>=32 && c<127)?c:'?'; i++;
    }
    r36_put32(record+20,i);
    r36_put32(record+508,r36_crc(record,508));
    long offset=(seq%R36_RECORD_COUNT)*R36_RECORD_SIZE;
    if(r36_raw(SYS_lseek,fd,offset,0)!=offset) goto done;
    if(r36_raw(SYS_write,fd,(long)record,sizeof(record))!=(long)sizeof(record)) goto done;
    result=r36_raw(SYS_fsync,fd,0,0);
done:
    (void)r36_raw(SYS_close,fd,0,0);
    return (int)result;
}
static int r36_boot_event(const char *event) {
#if defined(__mips__) && !defined(R36_BOOTLOG_NO_CONSOLE)
    /* UART/kernel output survives an unwritable SD only when an external
     * collector or kernel persistence is available. Never open host devices. */
    static const char prefix[]="LilSmoke R36 checkpoint: ";
    unsigned length=0;
    while(length<479 && event[length]) length++;
    const char *outputs[]={"/dev/kmsg","/dev/console"};
    for(unsigned i=0;i<2;i++) {
        long fd=r36_raw(SYS_open,(long)outputs[i],O_WRONLY|O_NONBLOCK|O_NOFOLLOW|O_CLOEXEC,0);
        if(fd>=0) {
            (void)r36_raw(SYS_write,fd,(long)prefix,sizeof(prefix)-1);
            (void)r36_raw(SYS_write,fd,(long)event,length);
            (void)r36_raw(SYS_write,fd,(long)"\n",1);
            (void)r36_raw(SYS_close,fd,0,0);
        }
    }
#endif
    int result=r36_journal_write(R36_JOURNAL_PATH,event);
    if(result<0) {
        static const char warning[]="LilSmoke R36 boot checkpoint could not be persisted\n";
        (void)r36_raw(SYS_write,2,(long)warning,sizeof(warning)-1);
    }
    return result;
}
#endif
