#define _GNU_SOURCE
#include <sys/socket.h>
#include <sys/eventfd.h>
#include <sys/timerfd.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <errno.h>
#include <SDL_hidapi.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int socket(int domain,int type,int protocol) {
    if(domain==AF_NETLINK) {errno=EAFNOSUPPORT;return -1;}
    return syscall(SYS_socket,domain,type,protocol);
}
int eventfd(unsigned value,int flags) {(void)value;(void)flags;errno=ENOSYS;return -1;}
int timerfd_create(int clock,int flags) {(void)clock;(void)flags;errno=ENOSYS;return -1;}

/* QA-only interposer: observe reports, without modifying device input. */
int SDL_hid_read_timeout(SDL_hid_device *device, unsigned char *data,
                         size_t length, int milliseconds) {
    typedef int (*read_fn)(SDL_hid_device *, unsigned char *, size_t, int);
    static read_fn read_next;
    static FILE *trace;
    static unsigned lines;
    static unsigned char previous[3];
    if (!read_next) read_next = (read_fn)dlsym(RTLD_NEXT, "SDL_hid_read_timeout");
    if (!read_next) return -1;
    int count = read_next(device, data, length, milliseconds);
    if (count > 0 && getenv("LILSMOKE_USB_QA_TRACE") && lines < 128 &&
        (lines == 0 || count < 3 || memcmp(data, previous, 3))) {
        if (!trace) trace = fopen("/dev/console", "w");
        if (trace) {
            fprintf(trace, "QA_USB_RAW length=%d bytes=", count);
            for (int i = 0; i < count && i < 7; ++i) fprintf(trace, "%02x", data[i]);
            fputc('\n', trace); fflush(trace);
        }
        if (count >= 3) memcpy(previous, data, 3);
        ++lines;
    }
    return count;
}
