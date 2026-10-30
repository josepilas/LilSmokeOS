#define _GNU_SOURCE
#include "hotplug_file.h"

static uint64_t test_now = 1000;
int __wrap_clock_gettime(clockid_t id, struct timespec *ts) {
    (void)id;
    ts->tv_sec = test_now / 1000; ts->tv_nsec = test_now % 1000 * 1000000; return 0;
}
int __wrap___clock_gettime64(clockid_t id, struct timespec *ts) { return __wrap_clock_gettime(id, ts); }
int legacy_read(struct ls_gp_state *out);
void legacy_close(void);

int main(void) {
    struct gp_fixture f; gp_setup(&f);
    struct ls_gp_reader reader = LS_GP_READER_INIT;
    struct ls_gp_state out, old;
    assert(!ls_gp_read(&reader, &out) && !out.pads[0].connected);
    gp_pad(&f, 0, 0x045e, 0x028e, 7);
    f.packet.pads[0].buttons = 1U << LS_GP_X;
    f.packet.pads[0].axes[0] = 16384;
    test_now += 100; gp_write(&f, test_now, 1);
    assert(ls_gp_read(&reader, &out) && out.pads[0].buttons);
    assert(legacy_read(&old) && !memcmp(&old, &out, sizeof(out)));
    uint64_t generation = reader.generation;
    /* Unchanged reports are still fresh after more than 1000 ms of idle. */
    for (unsigned i = 0; i < 150; ++i) {
        test_now += 8; gp_write(&f, test_now, 0);
        assert(ls_gp_read(&reader, &out) && out.pads[0].buttons && out.pads[0].axes[0] == 16384);
        assert(legacy_read(&old) && old.pads[0].buttons);
    }
    assert(reader.generation == generation);
    test_now += 501;
    assert(!ls_gp_read(&reader, &out) && !out.pads[0].buttons && !out.pads[0].axes[0]);
    assert(!legacy_read(&old) && !old.pads[0].connected);
    /* Replacement can reuse the PID and controller instance, but not inode. */
    test_now += 1000; gp_write(&f, test_now, 1);
    assert(ls_gp_read(&reader, &out) && reader.generation != generation);
    assert(legacy_read(&old) && old.pads[0].connected);
    f.packet.sequence |= 1; gp_write(&f, test_now, 0);
    assert(!ls_gp_read(&reader, &out) && !out.pads[0].connected);
    f.packet.sequence &= ~1U;
    gp_write(&f, test_now + 1, 0);
    assert(!ls_gp_read(&reader, &out));
    gp_write(&f, test_now, 0);
    assert(ls_gp_read(&reader, &out));
    ls_gp_close(&reader); legacy_close();
    struct ls_gp_reader restarted = LS_GP_READER_INIT;
    assert(ls_gp_read(&restarted, &out) && out.pads[0].connected);
    assert(!unlink(f.path)); test_now += 100;
    assert(!ls_gp_read(&restarted, &out) && !out.pads[0].connected);
    ls_gp_close(&restarted); gp_cleanup(&f);
    puts("PASS ABI-1 legacy/new readers: heartbeat >1000ms, stale, torn/future, replacement, consumer restart, removal");
}
