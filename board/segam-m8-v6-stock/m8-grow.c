/* Grow only a preallocated FAT32 companion volume, without moving its data.
 * The builder reserves FAT capacity. Unknown free counts are valid FAT32
 * FSInfo values. Writing them before the primary BPB makes retries safe.
 * No formatting, loop devices, modules, or SPI writes are involved. */
#define _GNU_SOURCE
#define _FILE_OFFSET_BITS 64
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <linux/blkpg.h>
#include <linux/fs.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>

static int metadata_changed;
static void die(const char *why) {
    fprintf(stderr, "M8Grow: %s errno=%d metadata_changed=%d\n", why, errno, metadata_changed);
    exit(metadata_changed ? 3 : 2);
}
static int boot_lock_owned(const char *directory) {
    struct stat st;
    char name[512], owner[32];
    if (!directory || lstat(directory, &st) || !S_ISDIR(st.st_mode) || st.st_uid != 0 ||
        (st.st_mode & 077) || snprintf(name, sizeof(name), "%s/owner", directory) >= (int)sizeof(name)) return 0;
    int fd = open(name, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) return 0;
    ssize_t count = -1;
    if (!fstat(fd, &st) && S_ISREG(st.st_mode) && st.st_uid == 0 && !(st.st_mode & 077))
        count = read(fd, owner, sizeof(owner) - 1);
    close(fd);
    if (count <= 0) return 0;
    owner[count] = 0;
    char *end;
    long parent = strtol(owner, &end, 10);
    return parent > 1 && parent == (long)getppid() && (*end == '\n' || *end == 0);
}
static int disk_lock(int fd) {
#ifdef M8_TEST_NO_FLOCK
    (void)fd; errno = ENOSYS; return -1;
#else
    return flock(fd, LOCK_EX | LOCK_NB);
#endif
}
static uint16_t u16(const unsigned char *p) { return p[0] | (uint16_t)p[1] << 8; }
static uint32_t u32(const unsigned char *p) {
    return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static void put32(unsigned char *p, uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) p[i] = (unsigned char)(value >> (i * 8));
}
static void read_sector(int fd, uint64_t sector, unsigned char *data) {
    if (pread(fd, data, 512, (off_t)(sector * 512)) != 512) die("sector read failed");
}
static void write_at(int fd, uint64_t offset, const void *data, size_t length) {
    metadata_changed = 1;
    if (pwrite(fd, data, length, (off_t)offset) != (ssize_t)length || fsync(fd)) die("metadata write failed");
    unsigned char check[512];
    if (length > sizeof(check) || pread(fd, check, length, (off_t)offset) != (ssize_t)length ||
        memcmp(data, check, length)) die("metadata readback differs");
}
static int fat32(const unsigned char *b) {
    return b[510] == 0x55 && b[511] == 0xaa && u16(b + 11) == 512 &&
           !memcmp(b + 82, "FAT32   ", 8) && b[13] && !(b[13] & (b[13] - 1));
}

int main(int argc, char **argv) {
    int testing = argc == 3 && !strcmp(argv[1], "--test-image");
    if (argc != (testing ? 3 : 2)) die("usage: m8-grow /dev/mmcblk0 (or --test-image FILE)");
    const char *path = argv[testing ? 2 : 1];
    if (!testing && strcmp(path, "/dev/mmcblk0")) die("only the console's SD device is accepted");
    int fd = open(path, O_RDWR | O_CLOEXEC | O_NOFOLLOW);
    struct stat st;
    if (fd < 0) die("cannot open disk");
    if (fstat(fd, &st)) die("cannot query disk type");
    if (disk_lock(fd)) {
        int unavailable = errno == ENOSYS || errno == EOPNOTSUPP;
        const char *lock = testing ? getenv("M8_TEST_BOOT_LOCK") : "/tmp/lilsmoke-m8-launch.lock";
        if (!unavailable || !boot_lock_owned(lock)) die("cannot establish exclusive boot ownership");
        puts("M8Grow: flock unavailable; validated parent-owned atomic boot lock");
    }
    uint64_t bytes;
    if (testing) {
        if (!S_ISREG(st.st_mode)) die("test target must be a regular file");
        bytes = (uint64_t)st.st_size;
    } else {
        int sector;
        if (!S_ISBLK(st.st_mode) || ioctl(fd, BLKGETSIZE64, &bytes) ||
            ioctl(fd, BLKSSZGET, &sector) || sector != 512) die("unexpected device geometry");
        /* Growth runs before the ROM partition is mounted. */
        FILE *mounts = fopen("/proc/self/mountinfo", "r");
        if (!mounts) die("cannot establish mount ownership");
        char line[4096];
        while (fgets(line, sizeof(line), mounts))
            if (strstr(line, " 179:2 ")) die("ROM partition is already mounted");
        fclose(mounts);
    }
    uint64_t sectors = bytes / 512;
    if (bytes % 512 || sectors > UINT32_MAX) die("unsupported disk capacity");
    unsigned char mbr[512], os[512], boot[512], backup[512], info[512], info_backup[512];
    read_sector(fd, 0, mbr);
    unsigned char *p1 = mbr + 446, *p2 = mbr + 462;
    uint32_t start = u32(p2 + 8), current = u32(p2 + 12);
    if (mbr[510] != 0x55 || mbr[511] != 0xaa || p1[4] != 0x0c || p2[4] != 0x0c ||
        u32(p1 + 8) != 2048 || !u32(p1 + 12) || !current ||
        (uint64_t)u32(p1 + 8) + u32(p1 + 12) != start ||
        (uint64_t)start + current > sectors) die("not a contiguous LILOS/FAT32 LILROMS disk");
    for (int i = 478; i < 510; ++i) if (mbr[i]) die("additional partitions present");
    read_sector(fd, 2048, os);
    read_sector(fd, start, boot);
    if (!fat32(os) || memcmp(os + 71, "LILOS      ", 11) ||
        !fat32(boot) || memcmp(boot + 71, "LILROMS    ", 11) || boot[16] != 2)
        die("filesystem/label mismatch; no changes made");
    uint32_t target = (uint32_t)(sectors - start), old = u32(boot + 32);
    uint32_t fat = u32(boot + 36), reserved = u16(boot + 14);
    uint32_t backup_sector = u16(boot + 50), info_sector = u16(boot + 48);
    uint64_t overhead = reserved + (uint64_t)boot[16] * fat;
    if (old > current || old <= overhead || target < old || !fat ||
        (old - overhead) / boot[13] < 65525 || target <= overhead ||
        (target - overhead) / boot[13] + 2 > (uint64_t)fat * 128 ||
        !backup_sector || !info_sector || backup_sector == info_sector ||
        backup_sector + info_sector >= reserved) die("FAT table lacks growth capacity or BPB is invalid");
    read_sector(fd, (uint64_t)start + backup_sector, backup);
    if (!fat32(backup) || u32(backup + 36) != fat || backup[13] != boot[13] ||
        (u32(backup + 32) != old && u32(backup + 32) != target)) die("backup BPB mismatch");
    read_sector(fd, (uint64_t)start + info_sector, info);
    read_sector(fd, (uint64_t)start + backup_sector + info_sector, info_backup);
    if (u32(info) != 0x41615252 || u32(info + 484) != 0x61417272 ||
        u32(info + 508) != 0xaa550000 || u32(info_backup) != 0x41615252 ||
        u32(info_backup + 484) != 0x61417272 || u32(info_backup + 508) != 0xaa550000)
        die("invalid FSInfo copies");
    printf("M8Grow: old_sectors=%"PRIu32" target_sectors=%"PRIu32" reserved_fat_sectors=%"PRIu32"\n", old, target, fat);
    if (old != target || current != target || u32(backup + 32) != target) {
        put32(info + 488, UINT32_MAX); put32(info + 492, UINT32_MAX);
        put32(info_backup + 488, UINT32_MAX); put32(info_backup + 492, UINT32_MAX);
        write_at(fd, ((uint64_t)start + info_sector) * 512, info, 512);
        write_at(fd, ((uint64_t)start + backup_sector + info_sector) * 512, info_backup, 512);
        put32(p2 + 12, target);
        write_at(fd, 462, p2, 16);
        put32(backup + 32, target);
        write_at(fd, ((uint64_t)start + backup_sector) * 512, backup, 512);
        put32(boot + 32, target);
        write_at(fd, (uint64_t)start * 512, boot, 512);
    }
    if (!testing) {
        /* The HC16xx driver accepted BLKPG but left the old block-device
         * geometry cached. Cold-boot once after a real metadata change. */
        if (metadata_changed) {
            puts("M8Grow: metadata verified; cold restart required before mounting the expanded volume");
            close(fd); return 75;
        }
        int partfd = open("/dev/mmcblk0p2", O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
        uint64_t visible = 0;
        if (partfd < 0 || ioctl(partfd, BLKGETSIZE64, &visible)) die("cannot query companion partition");
        close(partfd);
        if (visible != (uint64_t)target * 512) {
            struct blkpg_partition part = {0};
            part.pno = 2; part.start = (long long)start * 512; part.length = (long long)target * 512;
            struct blkpg_ioctl_arg arg = {BLKPG_RESIZE_PARTITION, 0, sizeof(part), &part};
            if (ioctl(fd, BLKPG, &arg)) {
                fprintf(stderr, "M8Grow: disk metadata verified; reboot required for kernel geometry\n");
                close(fd); return 75;
            }
        }
    }
    close(fd);
    puts("M8Grow: verified; existing FAT entries and file data were not rewritten");
    return 0;
}
