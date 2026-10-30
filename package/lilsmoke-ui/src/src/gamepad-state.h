/* MIT. Kernel-independent controller transport, not a keyboard/mouse mapper. */
#ifndef LILSMOKE_GAMEPAD_STATE_H
#define LILSMOKE_GAMEPAD_STATE_H
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#define LS_GP_MAGIC UINT32_C(0x4c534750)
#define LS_GP_SLOTS 4
enum { LS_GP_A, LS_GP_B, LS_GP_X, LS_GP_Y, LS_GP_BACK, LS_GP_GUIDE, LS_GP_START,
       LS_GP_L3, LS_GP_R3, LS_GP_LB, LS_GP_RB, LS_GP_UP, LS_GP_DOWN, LS_GP_LEFT, LS_GP_RIGHT };
struct ls_gp_pad {
    uint32_t connected, exported, instance, buttons;
    int16_t axes[6];
    uint16_t vendor, product, version, reserved;
    char name[64];
};
struct ls_gp_state {
    uint32_t magic, abi, sequence, pid;
    uint64_t millis;
    struct ls_gp_pad pads[LS_GP_SLOTS];
};
static inline uint64_t ls_gp_now(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts)) return 0;
    return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
}
/* Local reader metadata only. Keep the on-disk ABI-1 packet compatible with
 * cached applications, including the public SM64 game. */
struct ls_gp_reader { int fd; const struct ls_gp_state *map; uint64_t checked, generation; };
#define LS_GP_READER_INIT { -1, NULL, 0, 0 }
static inline void ls_gp_close(struct ls_gp_reader *r) {
    if (r->map) munmap((void *)r->map, sizeof(*r->map));
    if (r->fd >= 0) close(r->fd);
    r->map = NULL; r->fd = -1; r->checked = 0;
}
static inline int ls_gp_read(struct ls_gp_reader *r, struct ls_gp_state *out) {
    const char *path = getenv("LILSMOKE_GAMEPAD_STATE");
    struct stat st;
    const uint64_t now = ls_gp_now();
    if (!path) path = "/run/lilsmoke-gamepads.state";
    memset(out, 0, sizeof(*out));
    if (r->map && now - r->checked >= 100) {
        struct stat opened;
        r->checked = now;
        if (stat(path, &st) || fstat(r->fd, &opened) || st.st_size != sizeof(*out) ||
            st.st_ino != opened.st_ino || st.st_dev != opened.st_dev)
            ls_gp_close(r);
    }
    if (!r->map) {
        if (r->checked && now - r->checked < 100) return 0;
        r->checked = now;
        r->fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
        if (r->fd < 0) return 0;
        if (fstat(r->fd, &st) || !S_ISREG(st.st_mode) || st.st_uid != 0 ||
            (st.st_mode & 0022) || st.st_size != sizeof(*out)) { ls_gp_close(r); return 0; }
        void *map = mmap(NULL, sizeof(*out), PROT_READ, MAP_SHARED, r->fd, 0);
        if (map == MAP_FAILED) { ls_gp_close(r); return 0; }
        r->map = (const struct ls_gp_state *)map;
        ++r->generation;
    }
    for (unsigned attempt = 0; attempt < 3; ++attempt) {
        uint32_t seq = __atomic_load_n(&r->map->sequence, __ATOMIC_ACQUIRE);
        if (seq & 1) continue;
        memcpy(out, r->map, sizeof(*out));
        __atomic_thread_fence(__ATOMIC_ACQUIRE);
        if (seq != __atomic_load_n(&r->map->sequence, __ATOMIC_ACQUIRE)) continue;
        const uint64_t observed = ls_gp_now();
        if (out->magic == LS_GP_MAGIC && out->abi == 1 && out->millis <= observed && observed - out->millis <= 500)
            return 1;
        break;
    }
    memset(out, 0, sizeof(*out)); return 0;
}
#endif
