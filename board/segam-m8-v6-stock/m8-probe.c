#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <unistd.h>

static void dump(const char *path) {
    char data[4096];
    int fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    printf("--- %s ---\n", path);
    if (fd < 0) { printf("unavailable: %s\n", strerror(errno)); return; }
    ssize_t count;
    while ((count = read(fd, data, sizeof(data))) > 0)
        if (fwrite(data, 1, (size_t)count, stdout) != (size_t)count) break;
    close(fd);
    putchar('\n');
}

int main(int argc, char **argv) {
    if (argc == 3 && !strcmp(argv[1], "--rom-volume")) {
        unsigned char boot[512];
        struct stat st;
        int fd = open(argv[2], O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
        if (fd < 0) return 1;
        int good = !fstat(fd, &st) && S_ISBLK(st.st_mode) &&
            pread(fd, boot, sizeof(boot), 0) == (ssize_t)sizeof(boot) &&
            boot[510] == 0x55 && boot[511] == 0xaa &&
            !memcmp(boot + 82, "FAT32   ", 8) &&
            !memcmp(boot + 71, "LILROMS    ", 11);
        close(fd);
        return good ? 0 : 1;
    }
    if (argc != 1) return 64;
    struct utsname name;
    if (!uname(&name)) printf("Kernel=%s Machine=%s\n", name.release, name.machine);
    dump("/proc/cpuinfo");
    dump("/proc/meminfo");
    dump("/proc/filesystems");
    dump("/proc/partitions");
    dump("/proc/self/mountinfo");
    dump("/proc/asound/cards");
    dump("/proc/swaps");
    dump("/proc/fb");
    dump("/proc/bus/input/devices");
    const char *devices[] = {"/dev/fb0", "/dev/dis", "/dev/auddec", "/dev/sndC0i2so", "/dev/fuse", "/dev/input/event0"};
    for (unsigned i = 0; i < sizeof(devices) / sizeof(devices[0]); ++i) {
        struct stat st;
        printf("Device=%s Present=%d\n", devices[i], stat(devices[i], &st) == 0);
    }
    int fb = open("/dev/fb0", O_RDONLY | O_CLOEXEC);
    if (fb >= 0) {
        struct fb_var_screeninfo variable;
        struct fb_fix_screeninfo fixed;
        if (!ioctl(fb, FBIOGET_VSCREENINFO, &variable) && !ioctl(fb, FBIOGET_FSCREENINFO, &fixed))
            printf("Framebuffer=%ux%u BPP=%u LineBytes=%u MemoryBytes=%u\n",
                   variable.xres, variable.yres, variable.bits_per_pixel, fixed.line_length, fixed.smem_len);
        else printf("FramebufferQueryFailed=%s\n", strerror(errno));
        close(fb);
    }
    return 0;
}
