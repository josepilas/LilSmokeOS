#define _GNU_SOURCE
#include "../../package/opendingux-emulators/m8_pocketsnes_evdev.c"
#include "hotplug_file.h"
static uint64_t test_now = 1000;
static unsigned resets[2];
uint64_t m8_now_us(void) { return test_now * 1000; }
void m8_input_reset(unsigned player) { assert(player < 2); ++resets[player]; }
int __wrap_clock_gettime(clockid_t id, struct timespec *ts) {
    (void)id;
    ts->tv_sec = test_now / 1000; ts->tv_nsec = test_now % 1000 * 1000000; return 0;
}
int __wrap___clock_gettime64(clockid_t id, struct timespec *ts) { return __wrap_clock_gettime(id, ts); }
int main(void) {
    struct gp_fixture f; gp_setup(&f);
    for (unsigned i = 0; i < M8_DEVICES; ++i) devices[i].fd = -1;
    defaults(); next_scan = UINT64_MAX;
    uint32_t held;
    gp_pad(&f, 0, 0x045e, 0x028e, 1);
    f.packet.pads[0].buttons = (1U << LS_GP_START) | (1U << LS_GP_X);
    f.packet.pads[0].axes[0] = 32767;
    gp_write(&f, test_now, 1);
    assert(m8_evdev_poll(0, &held) && held == (SAL_INPUT_START | SAL_INPUT_X | SAL_INPUT_RIGHT));
    struct m8_chord chord = {0};
    assert(m8_chord_filter(&chord, held, test_now * 1000) == (SAL_INPUT_MENU | SAL_INPUT_RIGHT));
    gp_pad(&f, 1, 0x0079, 0x181c, 2); f.packet.pads[1].buttons = 1U << LS_GP_B;
    gp_write(&f, test_now, 0);
    assert(m8_evdev_poll(1, &held) && held == SAL_INPUT_B);
    f.packet.pads[0].connected = 0; gp_write(&f, test_now, 0);
    assert(m8_evdev_poll(0, &held) && !held);
    assert(m8_evdev_poll(1, &held) && held == SAL_INPUT_B);
    gp_pad(&f, 0, 0x1997, 0x2535, 3);
    f.packet.pads[0].buttons = (1U << LS_GP_START) | (1U << LS_GP_BACK);
    gp_write(&f, test_now, 0);
    assert(m8_evdev_poll(0, &held) && held == (SAL_INPUT_START | SAL_INPUT_SELECT));
    memset(&chord, 0, sizeof(chord));
    assert(m8_chord_filter(&chord, held, test_now * 1000) == M8_INPUT_EXIT);
    unsigned previous = resets[0];
    gp_pad(&f, 0, 0x0079, 0x181c, 3); f.packet.pads[0].buttons = 1U << LS_GP_A;
    gp_write(&f, test_now, 0); assert(m8_evdev_poll(0, &held) && held == SAL_INPUT_A);
    assert(resets[0] > previous);
    previous = resets[0]; test_now += 100; gp_write(&f, test_now, 1);
    assert(m8_evdev_poll(0, &held) && held == SAL_INPUT_A && resets[0] > previous);
    for (unsigned i = 0; i < 150; ++i) {
        test_now += 8; gp_write(&f, test_now, 0);
        assert(m8_evdev_poll(0, &held) && held == SAL_INPUT_A);
    }
    test_now += 501;
    assert(m8_evdev_poll(0, &held) && !held); /* authoritative neutral, never stale SDL fallback */
    assert(m8_evdev_poll(1, &held) && !held);
    m8_evdev_close(); next_scan = UINT64_MAX;
    test_now += 100; gp_write(&f, test_now, 1);
    assert(m8_evdev_poll(0, &held) && held == SAL_INPUT_A);
    m8_evdev_close(); gp_cleanup(&f);
    puts("PASS PocketSNES boot-X/shared input: two players, X/D/PS identity, chords, heartbeat, generation, stale-neutral, consumer restart");
}
