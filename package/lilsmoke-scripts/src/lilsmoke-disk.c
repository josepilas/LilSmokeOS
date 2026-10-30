/* Small MBR-only helper. Growth never changes a partition start or p1. */
#define _GNU_SOURCE
#define _FILE_OFFSET_BITS 64
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <linux/fs.h>
#include <linux/blkpg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>

static void die(const char *message) { fprintf(stderr, "lilsmoke-disk: %s\n", message); exit(1); }
static uint32_t le32(const unsigned char *p) {
    return (uint32_t)p[0] | (uint32_t)p[1]<<8 | (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24;
}
static void put32(unsigned char *p, uint32_t n) {
    for (unsigned i=0; i<4; ++i) p[i]=(unsigned char)(n>>(8*i));
}
int main(int argc, char **argv) {
    int grow=argc==3 && !strcmp(argv[1], "grow");
    if (argc!=3 || (!grow && strcmp(argv[1], "info"))) die("usage: lilsmoke-disk info|grow DISK");
    int fd=open(argv[2], (grow ? O_RDWR : O_RDONLY) | O_CLOEXEC | O_NOFOLLOW);
    if (fd<0 || flock(fd, LOCK_EX|LOCK_NB)) die("cannot lock disk");
    struct stat st;
    if (fstat(fd,&st) || (!S_ISBLK(st.st_mode) && !S_ISREG(st.st_mode))) die("not a disk or image");
    uint64_t bytes;
    if (S_ISBLK(st.st_mode)) {
        int sector;
        if (ioctl(fd,BLKGETSIZE64,&bytes) || ioctl(fd,BLKSSZGET,&sector) || sector!=512)
            die("only 512-byte logical sectors are supported");
    } else bytes=(uint64_t)st.st_size;
    uint64_t sectors=bytes/512;
    if (sectors>UINT32_MAX || sectors<4096) die("disk outside supported MBR size range");
    unsigned char mbr[512];
    if (pread(fd,mbr,sizeof mbr,0)!=sizeof mbr || mbr[510]!=0x55 || mbr[511]!=0xaa)
        die("invalid MBR");
    unsigned char *a=mbr+446, *b=mbr+462;
    uint32_t start1=le32(a+8), count1=le32(a+12), start2=le32(b+8), count2=le32(b+12);
    if (a[4]!=0x0c || (b[4]!=0x07 && b[4]!=0x0c) || start1!=2048 || !count1 || !count2 ||
        (uint64_t)start1+count1!=start2 || (uint64_t)start2+count2>sectors)
        die("not the contiguous LILOS/LILROMS layout");
    for (int i=478;i<510;++i) if (mbr[i]) die("more than two partitions");
    uint32_t wanted=(uint32_t)(sectors-start2);
    printf("p1_start=%"PRIu32"\np1_sectors=%"PRIu32"\np2_start=%"PRIu32
           "\np2_sectors=%"PRIu32"\np2_target_sectors=%"PRIu32"\n", start1,count1,start2,count2,wanted);
    if (grow) {
        if (wanted<count2) die("refusing shrink");
        if (wanted!=count2) {
            /* Only the p2 entry is replaced; the bootstrap and p1 are untouched. */
            put32(b+12,wanted); b[5]=0xfe; b[6]=0xff; b[7]=0xff;
            if (pwrite(fd,b,16,462)!=16 || fsync(fd)) die("partition write failed");
        }
        if (S_ISBLK(st.st_mode)) {
            struct blkpg_partition part={0};
            part.pno=2; part.start=(long long)start2*512; part.length=(long long)wanted*512;
            struct blkpg_ioctl_arg arg={BLKPG_RESIZE_PARTITION,0,sizeof part,&part};
            if (ioctl(fd,BLKPG,&arg)) die("partition table saved; reboot required to update kernel geometry");
        }
    }
    close(fd); return 0;
}
