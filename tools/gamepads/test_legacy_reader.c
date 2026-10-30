/* Frozen pre-hotplug-fix reader algorithm, as embedded in the cached game.
   Uses the unchanged wire structs, not the new reader or generation metadata. */
#include "gamepad-state.h"
static struct { int fd; const struct ls_gp_state *map; uint64_t checked; } r = {-1, NULL, 0};
void legacy_close(void) {
    if (r.map) munmap((void *)r.map, sizeof(*r.map));
    if (r.fd >= 0) close(r.fd);
    r.map = NULL; r.fd = -1; r.checked = 0;
}
int legacy_read(struct ls_gp_state *out) {
    const char *path = getenv("LILSMOKE_GAMEPAD_STATE");
    struct stat st;
    const uint64_t now = ls_gp_now();
    if (!path) path = "/run/lilsmoke-gamepads.state";
    memset(out, 0, sizeof(*out));
    if (r.map && now - r.checked >= 1000) {
        struct stat opened;
        r.checked = now;
        if (stat(path, &st) || fstat(r.fd, &opened) || st.st_ino != opened.st_ino || st.st_dev != opened.st_dev)
            legacy_close();
    }
    if (!r.map) {
        if (r.checked && now - r.checked < 1000) return 0;
        r.checked = now;
        r.fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
        if (r.fd < 0) return 0;
        if (fstat(r.fd, &st) || !S_ISREG(st.st_mode) || st.st_uid != 0 ||
            (st.st_mode & 0022) || st.st_size != sizeof(*out)) { legacy_close(); return 0; }
        void *map = mmap(NULL, sizeof(*out), PROT_READ, MAP_SHARED, r.fd, 0);
        if (map == MAP_FAILED) { legacy_close(); return 0; }
        r.map = map;
    }
    for (unsigned attempt = 0; attempt < 3; ++attempt) {
        uint32_t seq = __atomic_load_n(&r.map->sequence, __ATOMIC_ACQUIRE);
        if (seq & 1) continue;
        memcpy(out, r.map, sizeof(*out));
        __atomic_thread_fence(__ATOMIC_ACQUIRE);
        if (seq != __atomic_load_n(&r.map->sequence, __ATOMIC_ACQUIRE)) continue;
        const uint64_t observed = ls_gp_now();
        if (out->magic == LS_GP_MAGIC && out->abi == 1 && out->millis <= observed && observed - out->millis <= 500)
            return 1;
        break;
    }
    memset(out, 0, sizeof(*out)); return 0;
}
