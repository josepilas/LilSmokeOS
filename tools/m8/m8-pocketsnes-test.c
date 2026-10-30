#include <assert.h>
#include <stdio.h>
#include "m8_pocketsnes_evdev.c"

uint64_t m8_now_us(void) { return 1000000; }
void m8_input_reset(unsigned player) { assert(player < 2); }

static void test_pacing(void)
{
    struct m8_pacer p;
    uint64_t wait, now = 0;
    int i, renders = 0, consecutive = 0, maximum = 0;
    m8_pacer_reset(&p, now, 16667);
    for (i = 0; i < 60; ++i) {
        now += 1000;
        assert(m8_pacer_step(&p, now, -1, &wait));
        now += wait;
        ++renders;
    }
    assert(now == 1000020 && renders == 60);
    printf("fast CPU: 60 guest/render frames in %llu us (not unthrottled)\n", (unsigned long long)now);
    m8_pacer_reset(&p, 0, 16667);
    for (i = 0; i < 120; ++i) {
        int render = m8_pacer_step(&p, (i + 1) * 40000ULL, -1, &wait);
        assert(wait == 0);
        if (render) consecutive = 0;
        else if (++consecutive > maximum) maximum = consecutive;
        assert(consecutive <= M8_MAX_SKIP);
        assert((i + 1) * 40000ULL - p.deadline <= (uint64_t)p.period * M8_MAX_SKIP);
    }
    assert(maximum == M8_MAX_SKIP);
    printf("overloaded CPU: max consecutive skips=%d, bounded backlog\n", maximum);
    m8_pacer_reset(&p, 0, 20000);
    for (i = 0; i < 50; ++i) {
        m8_pacer_step(&p, i * 20000ULL + 1000, -1, &wait);
        assert(wait == 19000);
    }
    m8_pacer_reset(&p, 0, 16667);
    renders = 0;
    for (i = 0; i < 60; ++i) renders += m8_pacer_step(&p, i * 16667ULL, 2, &wait);
    assert(renders == 20);
    puts("PAL 50 Hz and manual 1-in-3 rendering: PASS");
}

static void test_hotkeys(void)
{
    struct m8_chord c = {0};
    uint32_t menu = SAL_INPUT_START | SAL_INPUT_X;
    uint32_t exit_combo = SAL_INPUT_START | SAL_INPUT_SELECT;
    assert(m8_chord_filter(&c, SAL_INPUT_START, 1000) == 0);
    assert(m8_chord_filter(&c, menu, 2000) == SAL_INPUT_MENU);
    assert(m8_chord_filter(&c, menu, 3000) == 0);
    assert(m8_chord_filter(&c, SAL_INPUT_X, 4000) == 0);
    assert(m8_chord_filter(&c, SAL_INPUT_START, 5000) == 0);
    assert(m8_chord_filter(&c, 0, 6000) == 0);
    assert(m8_chord_filter(&c, menu, 7000) == SAL_INPUT_MENU);
    assert(m8_chord_filter(&c, 0, 8000) == 0);
    assert(m8_chord_filter(&c, exit_combo, 9000) == M8_INPUT_EXIT);
    assert(m8_chord_filter(&c, SAL_INPUT_SELECT, 10000) == 0);
    assert(m8_chord_filter(&c, exit_combo, 11000) == 0);
    assert(m8_chord_filter(&c, 0, 12000) == 0);
    assert(m8_chord_filter(&c, SAL_INPUT_START, 13000) == 0);
    assert(m8_chord_filter(&c, 0, 14000) == SAL_INPUT_START);
    assert(m8_chord_filter(&c, SAL_INPUT_START, 15000) == 0);
    assert(m8_chord_filter(&c, SAL_INPUT_START, 95000) == SAL_INPUT_START);
    memset(&c, 0, sizeof(c));
    assert(m8_chord_filter(&c, exit_combo | SAL_INPUT_X, 1000) & M8_INPUT_EXIT);
    puts("hotkeys: edge-only, Start tap/hold, exit priority, partial-release suppression: PASS");
}

static void key_event(struct m8_device *d, int code, int value)
{
    struct m8_input_event e = {0};
    e.type = EV_KEY; e.code = code; e.value = value;
    consume(d, &e);
}

static void test_evdev(const char *map)
{
    struct m8_device d = {0};
    struct m8_input_event event = {0};
    struct m8_chord chord = {0};
    uint32_t held;
    int pipes[2], i;
    defaults();
    assert(m8_evdev_key(KEY_LEFTCTRL) == SAL_INPUT_A);
    assert(m8_evdev_key(KEY_LEFTALT) == SAL_INPUT_B);
    assert(m8_evdev_key(KEY_SPACE) == SAL_INPUT_X);
    assert(m8_evdev_key(KEY_LEFTSHIFT) == SAL_INPUT_Y);
    assert(m8_evdev_key(BTN_WEST) == SAL_INPUT_X);
    assert(m8_evdev_load_map(map) == 7);
    assert(m8_evdev_key(BTN_SOUTH) == SAL_INPUT_B);
    assert(m8_evdev_key(BTN_EAST) == SAL_INPUT_A);
    assert(m8_evdev_key(KEY_VOLUMEUP) == 0);
    assert(m8_evdev_key(KEY_MAX + 1) == 0);
    key_event(&d, BTN_SOUTH, 1);
    key_event(&d, KEY_LEFTALT, 1);
    key_event(&d, BTN_SOUTH, 0);
    assert(device_held(&d) == SAL_INPUT_B);
    key_event(&d, KEY_LEFTALT, 0);
    key_event(&d, BTN_START, 1); key_event(&d, BTN_WEST, 1);
    assert(m8_chord_filter(&chord, device_held(&d), 1000) == SAL_INPUT_MENU);
    key_event(&d, BTN_START, 0);
    assert(m8_chord_filter(&chord, device_held(&d), 2000) == 0);
    key_event(&d, BTN_WEST, 0);
    assert(m8_chord_filter(&chord, device_held(&d), 3000) == 0);
    key_event(&d, BTN_START, 1); key_event(&d, BTN_SELECT, 1);
    assert(m8_chord_filter(&chord, device_held(&d), 4000) == M8_INPUT_EXIT);
    d.present[0] = 1; d.minimum[0] = 0; d.maximum[0] = 255; d.axis[0] = 128;
    d.present[2] = 1; d.minimum[2] = -1; d.maximum[2] = 1; d.axis[2] = -1;
    assert(device_held(&d) & SAL_INPUT_LEFT);
    d.axis[2] = 0; d.axis[0] = 255;
    assert(device_held(&d) & SAL_INPUT_RIGHT);
    event.type = EV_SYN; event.code = SYN_DROPPED; consume(&d, &event);
    assert(device_held(&d) == 0);
    d.fd = -1;
    event.code = SYN_REPORT; consume(&d, &event);
    assert(!d.dropped && device_held(&d) == 0);
    for (i = 0; i < M8_DEVICES; ++i) devices[i].fd = -1;
    next_scan = UINT64_MAX;
    assert(pipe(pipes) == 0);
    fcntl(pipes[0], F_SETFL, O_NONBLOCK);
    devices[0].fd = pipes[0]; devices[0].player = 0;
    event.type = EV_KEY; event.code = BTN_START; event.value = 1;
    assert(write(pipes[1], &event, sizeof(event)) == sizeof(event));
    event.code = BTN_SELECT;
    assert(write(pipes[1], &event, sizeof(event)) == sizeof(event));
    assert(m8_evdev_poll(0, &held) && held == (SAL_INPUT_START | SAL_INPUT_SELECT));
    assert(m8_evdev_poll(1, &held) && held == 0);
    close(pipes[1]);
    assert(!m8_evdev_poll(0, &held) && held == 0);
    puts("evdev: keyboard/Android BTN hotkeys, map overrides, duplicate keys, axes, SYN_DROPPED, read/unplug: PASS");
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    test_pacing(); test_hotkeys(); test_evdev(argv[1]);
    puts("M8 PocketSNES focused host tests: PASS");
    return 0;
}
