#ifndef GP_HOTPLUG_FILE_H
#define GP_HOTPLUG_FILE_H
#include "gamepad-state.h"
#include <assert.h>
#include <stddef.h>
#include <stdio.h>

struct gp_fixture {
    char directory[128], path[192], next[208];
    struct ls_gp_state packet;
};
static void gp_setup(struct gp_fixture *f) {
    memset(f, 0, sizeof(*f));
    strcpy(f->directory, "/tmp/lilsmoke-hotplug-XXXXXX");
    assert(mkdtemp(f->directory));
    snprintf(f->path, sizeof(f->path), "%s/state", f->directory);
    snprintf(f->next, sizeof(f->next), "%s.next", f->path);
    assert(!setenv("LILSMOKE_GAMEPAD_STATE", f->path, 1));
    f->packet.magic = LS_GP_MAGIC; f->packet.abi = 1; f->packet.pid = 42;
    assert(sizeof(struct ls_gp_pad) == 100 && sizeof(f->packet) == 424);
    assert(offsetof(struct ls_gp_state, pads) == 24);
}
static void gp_write(struct gp_fixture *f, uint64_t now, int replace) {
    f->packet.millis = now;
    f->packet.sequence += 2;
    int fd = open(replace ? f->next : f->path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    assert(fd >= 0);
    assert(!fchmod(fd, 0644));
    assert(write(fd, &f->packet, sizeof(f->packet)) == (ssize_t)sizeof(f->packet));
    assert(!close(fd));
    if (replace) assert(!rename(f->next, f->path));
}
static void gp_pad(struct gp_fixture *f, unsigned slot, unsigned vendor, unsigned product, unsigned instance) {
    struct ls_gp_pad *pad = &f->packet.pads[slot];
    memset(pad, 0, sizeof(*pad)); pad->connected = 1;
    pad->vendor = vendor; pad->product = product; pad->instance = instance;
    snprintf(pad->name, sizeof(pad->name), "Fixture %04x:%04x", vendor, product);
}
static void gp_cleanup(struct gp_fixture *f) {
    unlink(f->path); unlink(f->next); rmdir(f->directory);
}
#endif
