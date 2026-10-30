#include "m8_pocketsnes.h"
#include "gamepad-state.h"
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define M8_DEVICES 8
#define KEY_BYTES ((KEY_MAX + 8) / 8)
#define BIT_SET(bits, code) ((bits)[(code) / 8] & (1U << ((code) % 8)))

/* Linux 4.4 sends 16-byte events to 32-bit MIPS, including musl time64 users.
   Never read these using musl's 64-bit-time struct input_event. */
#if __SIZEOF_LONG__ == 4
struct m8_input_event {
    uint32_t sec, usec;
    uint16_t type, code;
    int32_t value;
};
_Static_assert(sizeof(struct m8_input_event) == 16, "Linux 4.4 input wire ABI");
#else
#define m8_input_event input_event
#endif

struct m8_device {
    int fd, index, player, pad, dropped;
    uint64_t usb_generation;
    uint32_t usb_instance, usb_pid;
    uint16_t usb_vendor, usb_product, usb_version;
    unsigned char keys[KEY_BYTES];
    int axis[4], minimum[4], maximum[4], present[4];
};
static struct m8_device devices[M8_DEVICES];
static const int axis_codes[4] = { ABS_X, ABS_Y, ABS_HAT0X, ABS_HAT0Y };
static uint32_t keymap[KEY_MAX + 1];
static uint64_t next_scan;
static struct ls_gp_reader usb_reader = LS_GP_READER_INIT;
static int usb_authoritative;

static uint32_t action(const char *name, int *valid)
{
    static const struct { const char *name; uint32_t mask; } names[] = {
        { "up", SAL_INPUT_UP }, { "down", SAL_INPUT_DOWN },
        { "left", SAL_INPUT_LEFT }, { "right", SAL_INPUT_RIGHT },
        { "a", SAL_INPUT_A }, { "b", SAL_INPUT_B },
        { "x", SAL_INPUT_X }, { "y", SAL_INPUT_Y },
        { "l1", SAL_INPUT_L }, { "r1", SAL_INPUT_R },
        { "start", SAL_INPUT_START }, { "select", SAL_INPUT_SELECT },
        { "home", SAL_INPUT_MENU }, { "menu", SAL_INPUT_MENU },
        { "l2", 0 }, { "r2", 0 }, { "l3", 0 }, { "r3", 0 },
        { "vol_up", 0 }, { "vol_down", 0 }, { "power", 0 }
    };
    unsigned i;
    *valid = 1;
    for (i = 0; i < sizeof(names) / sizeof(names[0]); ++i)
        if (!strcmp(name, names[i].name)) return names[i].mask;
    *valid = 0;
    return 0;
}

int m8_evdev_load_map(const char *path)
{
    FILE *file = fopen(path, "r");
    char line[256], source[16], number[32], name[32], *end;
    int count = 0, valid;
    long code;
    if (!file) return 0;
    while (fgets(line, sizeof(line), file)) {
        uint32_t mask;
        if (!strchr(line, '\n') && !feof(file)) {
            int c;
            while ((c = fgetc(file)) != '\n' && c != EOF) {}
            continue;
        }
        if (sscanf(line, "%15s %31s %31s", source, number, name) != 3 || strcmp(source, "evdev")) continue;
        errno = 0;
        code = strtol(number, &end, 10);
        if (errno || *end || code < 0 || code > KEY_MAX) continue;
        mask = action(name, &valid);
        if (!valid) continue;
        keymap[code] = mask;
        ++count;
    }
    fclose(file);
    if (count) fprintf(stderr, "POCKETSNES_INPUT_MAP path=%s bindings=%d\n", path, count);
    return count;
}

uint32_t m8_evdev_key(unsigned code)
{
    return code <= KEY_MAX ? keymap[code] : 0;
}

static void defaults(void)
{
    memset(keymap, 0, sizeof(keymap));
    keymap[KEY_UP] = SAL_INPUT_UP; keymap[KEY_DOWN] = SAL_INPUT_DOWN;
    keymap[KEY_LEFT] = SAL_INPUT_LEFT; keymap[KEY_RIGHT] = SAL_INPUT_RIGHT;
    keymap[KEY_LEFTCTRL] = SAL_INPUT_A; keymap[KEY_LEFTALT] = SAL_INPUT_B;
    keymap[KEY_SPACE] = SAL_INPUT_X; keymap[KEY_LEFTSHIFT] = SAL_INPUT_Y;
    keymap[KEY_TAB] = keymap[KEY_PAGEDOWN] = SAL_INPUT_L;
    keymap[KEY_BACKSPACE] = keymap[KEY_PAGEUP] = SAL_INPUT_R;
    keymap[KEY_ENTER] = SAL_INPUT_START; keymap[KEY_ESC] = SAL_INPUT_SELECT;
    keymap[KEY_HOME] = keymap[KEY_END] = SAL_INPUT_MENU;
    keymap[BTN_SOUTH] = SAL_INPUT_A; keymap[BTN_EAST] = SAL_INPUT_B;
    keymap[BTN_WEST] = SAL_INPUT_X; keymap[BTN_NORTH] = SAL_INPUT_Y;
    keymap[BTN_TL] = SAL_INPUT_L; keymap[BTN_TR] = SAL_INPUT_R;
    keymap[BTN_SELECT] = SAL_INPUT_SELECT; keymap[BTN_START] = SAL_INPUT_START;
    keymap[BTN_MODE] = SAL_INPUT_MENU;
    keymap[BTN_DPAD_UP] = SAL_INPUT_UP; keymap[BTN_DPAD_DOWN] = SAL_INPUT_DOWN;
    keymap[BTN_DPAD_LEFT] = SAL_INPUT_LEFT; keymap[BTN_DPAD_RIGHT] = SAL_INPUT_RIGHT;
}

static void snapshot(struct m8_device *d)
{
    unsigned i;
    memset(d->keys, 0, sizeof(d->keys));
    ioctl(d->fd, EVIOCGKEY(sizeof(d->keys)), d->keys);
    for (i = 0; i < 4; ++i) {
        struct input_absinfo info;
        d->present[i] = ioctl(d->fd, EVIOCGABS(axis_codes[i]), &info) == 0;
        if (d->present[i]) {
            d->axis[i] = info.value;
            d->minimum[i] = info.minimum;
            d->maximum[i] = info.maximum;
        }
    }
}

static void disconnect(struct m8_device *d)
{
    if (d->fd != -1 && d->pad) m8_input_reset((unsigned)d->player);
    if (d->fd >= 0) {
        ioctl(d->fd, EVIOCGRAB, 0);
        close(d->fd);
    }
    memset(d, 0, sizeof(*d));
    d->fd = -1;
}

static void scan(void)
{
    int index, i, pads = 0;
    for (i = 0; i < M8_DEVICES; ++i)
        if (devices[i].fd != -1 && devices[i].pad) ++pads;
    for (index = 0; index < 32; ++index) {
        unsigned char bits[KEY_BYTES] = {0};
        char path[64], name[128] = {0};
        int slot = -1, known = 0, fd, pad;
        for (i = 0; i < M8_DEVICES; ++i) {
            if (devices[i].fd >= 0 && devices[i].index == index) known = 1;
            if (devices[i].fd == -1 && slot < 0) slot = i;
        }
        if (known || slot < 0) continue;
        snprintf(path, sizeof(path), "/dev/input/event%d", index);
        fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) continue;
        ioctl(fd, EVIOCGNAME(sizeof(name) - 1), name);
        if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(bits)), bits) < 0) { close(fd); continue; }
        pad = BIT_SET(bits, BTN_SOUTH) || strstr(name, "Controller") || strstr(name, "Gamepad");
        if ((!pad && !BIT_SET(bits, KEY_ENTER) && !BIT_SET(bits, KEY_UP)) || (pad && pads >= 2)) {
            close(fd); continue;
        }
        if (ioctl(fd, EVIOCGRAB, 1) != 0) {
            fprintf(stderr, "POCKETSNES_INPUT_BUSY path=%s error=%s\n", path, strerror(errno));
            close(fd); continue;
        }
        devices[slot].fd = fd;
        devices[slot].index = index;
        devices[slot].pad = !!pad;
        devices[slot].player = 0;
        if (pad) {
            for (i = 0; i < M8_DEVICES; ++i)
                if (i != slot && devices[i].fd != -1 && devices[i].pad && devices[i].player == 0)
                    devices[slot].player = 1;
            ++pads;
        }
        snapshot(&devices[slot]);
        fprintf(stderr, "POCKETSNES_INPUT path=%s name=%s player=%d grab=%d wire_bytes=%u\n",
                path, name, devices[slot].player + 1, 1,
                (unsigned)sizeof(struct m8_input_event));
    }
}

void m8_evdev_init(void)
{
    int i;
    defaults();
    ls_gp_close(&usb_reader);
    usb_authoritative = 0;
    m8_evdev_load_map("/persist/config/input-maps/user.map");
    for (i = 0; i < M8_DEVICES; ++i) devices[i].fd = -1;
    scan();
    next_scan = m8_now_us() + 1000000;
}

void m8_evdev_close(void)
{
    int i;
    for (i = 0; i < M8_DEVICES; ++i) disconnect(&devices[i]);
    ls_gp_close(&usb_reader);
    usb_authoritative = 0;
}

static void poll_usb(void)
{
    struct ls_gp_state packet;
    static const unsigned keys[] = {BTN_SOUTH, BTN_EAST, BTN_WEST, BTN_NORTH, BTN_SELECT,
        BTN_MODE, BTN_START, BTN_THUMBL, BTN_THUMBR, BTN_TL, BTN_TR,
        BTN_DPAD_UP, BTN_DPAD_DOWN, BTN_DPAD_LEFT, BTN_DPAD_RIGHT};
    unsigned slot, i;
    if (ls_gp_read(&usb_reader, &packet)) usb_authoritative = 1;
    for (slot = 0; slot < LS_GP_SLOTS; ++slot) {
        const struct ls_gp_pad *pad = &packet.pads[slot];
        struct m8_device *d = NULL;
        for (i = 0; i < M8_DEVICES; ++i)
            if (devices[i].fd == -2 && devices[i].index == (int)slot) d = &devices[i];
        if (!pad->connected || pad->exported) { if (d) disconnect(d); continue; }
        if (d && (d->usb_generation != usb_reader.generation || d->usb_pid != packet.pid ||
                  d->usb_instance != pad->instance || d->usb_vendor != pad->vendor ||
                  d->usb_product != pad->product || d->usb_version != pad->version)) {
            disconnect(d); d = NULL;
        }
        if (!d) {
            unsigned player;
            for (player = 0; player < 2; ++player) {
                for (i = 0; i < M8_DEVICES; ++i)
                    if (devices[i].fd != -1 && devices[i].pad && devices[i].player == (int)player) break;
                if (i == M8_DEVICES) break;
            }
            if (player == 2) continue;
            for (i = 0; i < M8_DEVICES; ++i) if (devices[i].fd == -1) { d = &devices[i]; break; }
            if (!d) continue;
            memset(d, 0, sizeof(*d)); d->fd = -2; d->pad = 1; d->index = (int)slot; d->player = (int)player;
            d->usb_generation = usb_reader.generation; d->usb_pid = packet.pid;
            d->usb_instance = pad->instance; d->usb_vendor = pad->vendor;
            d->usb_product = pad->product; d->usb_version = pad->version;
            for (i = 0; i < 2; ++i) { d->present[i] = 1; d->minimum[i] = -32768; d->maximum[i] = 32767; }
            fprintf(stderr, "POCKETSNES_INPUT shared_slot=%u name=%.64s player=%u instance=%u\n",
                    slot, pad->name, player + 1, pad->instance);
        }
        memset(d->keys, 0, sizeof(d->keys));
        for (i = 0; i < sizeof(keys)/sizeof(keys[0]); ++i)
            if (pad->buttons & (1U << i)) d->keys[keys[i]/8] |= 1U << (keys[i]%8);
        d->axis[0] = pad->axes[0]; d->axis[1] = pad->axes[1];
    }
}

static void consume(struct m8_device *d, const struct m8_input_event *e)
{
    unsigned i;
    if (e->type == EV_SYN && e->code == SYN_DROPPED) { d->dropped = 1; return; }
    if (d->dropped) {
        if (e->type == EV_SYN && e->code == SYN_REPORT) { snapshot(d); d->dropped = 0; }
        return;
    }
    if (e->type == EV_KEY && e->code <= KEY_MAX) {
        if (e->value) d->keys[e->code / 8] |= 1U << (e->code % 8);
        else d->keys[e->code / 8] &= ~(1U << (e->code % 8));
    } else if (e->type == EV_ABS) {
        for (i = 0; i < 4; ++i) if (e->code == axis_codes[i]) d->axis[i] = e->value;
    }
}

static uint32_t device_held(const struct m8_device *d)
{
    uint32_t held = 0;
    unsigned code, i;
    if (d->dropped) return 0;
    for (code = 0; code <= KEY_MAX; ++code)
        if (BIT_SET(d->keys, code)) held |= keymap[code];
    for (i = 0; i < 4; ++i) {
        int64_t center, delta, deadzone;
        if (!d->present[i]) continue;
        center = ((int64_t)d->minimum[i] + d->maximum[i]) / 2;
        delta = (int64_t)d->axis[i] - center;
        deadzone = i >= 2 ? 0 : ((int64_t)d->maximum[i] - d->minimum[i]) / 4;
        if (delta < -deadzone) held |= (i % 2) ? SAL_INPUT_UP : SAL_INPUT_LEFT;
        if (delta > deadzone) held |= (i % 2) ? SAL_INPUT_DOWN : SAL_INPUT_RIGHT;
    }
    return held;
}

int m8_evdev_poll(unsigned player, uint32_t *held)
{
    int i, active = 0;
    uint64_t now = m8_now_us();
    *held = 0;
    for (i = 0; i < M8_DEVICES; ++i) {
        struct m8_device *d = &devices[i];
        int batch;
        if (d->fd < 0) continue;
        for (batch = 0; batch < 16; ++batch) {
            struct m8_input_event events[16];
            ssize_t bytes = read(d->fd, events, sizeof(events));
            unsigned e;
            if (bytes < 0 && errno == EINTR) continue;
            if (bytes < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) break;
            if (bytes <= 0 || bytes % sizeof(events[0])) { disconnect(d); break; }
            for (e = 0; e < (unsigned)bytes / sizeof(events[0]); ++e) consume(d, &events[e]);
        }
    }
    poll_usb();
    if (now >= next_scan) { scan(); next_scan = now + 1000000; }
    for (i = 0; i < M8_DEVICES; ++i) {
        struct m8_device *d = &devices[i];
        if (d->fd != -1) {
            ++active;
            if ((unsigned)d->player == player) *held |= device_held(d);
        }
    }
    /* Never fall back to an SDL joystick handle after daemon loss/unplug.
       SDL1.2 may retain the old handle and replay a held button indefinitely. */
    return active != 0 || usb_authoritative;
}
