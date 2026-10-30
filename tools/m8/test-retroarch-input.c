#define _GNU_SOURCE
#define M8_LILSMOKE_TEST_16
#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include "input/input_driver.h"
#include "tasks/tasks_internal.h"
#include "verbosity.h"
#include "../../package/retroarch-mips/m8_lilsmoke_input.h"

static struct fixture {
    int present, grab, key_error, gone, storm;
    unsigned read_calls, chunk;
    unsigned char capabilities[M8_LS_KEY_BYTES], keys[M8_LS_KEY_BYTES];
    unsigned char abs_bits[(ABS_MAX + 8) / 8];
    struct input_absinfo axes[ABS_CNT];
    struct m8_ls_event queue[256];
    size_t bytes, position;
    const char *name;
} fixture[32];
static uint64_t clock_ms;
static unsigned connects, disconnects, closes;
static const char *root;

static void bit(unsigned char *bits, unsigned code) { bits[code / 8] |= 1U << (code % 8); }
static const char *redirect(const char *path, char *out, size_t size) {
    const char *tail = NULL;
    if (!strncmp(path, "/etc/lilsmoke/input-maps", 24)) tail = path + 24;
    if (tail) { snprintf(out, size, "%s/builtin%s", root, tail); return out; }
    if (!strncmp(path, "/persist/config/input-maps", 26)) tail = path + 26;
    if (tail) { snprintf(out, size, "%s/user%s", root, tail); return out; }
    if (!strncmp(path, "/persist/config/gamepads/", 24)) {
        snprintf(out, size, "%s/gamepads/%s", root, path + 24); return out;
    }
    return path;
}
static int mock_open(const char *path, int flags, ...) {
    char mapped[1024];
    if (!strcmp(path, "/etc/lilsmoke/input-maps/trusted.map") ||
        !strcmp(path, "/etc/lilsmoke/input-maps/untrusted.map")) { errno = ELOOP; return -1; }
    if (!strcmp(path, "/os/files/test-map.bin")) {
        snprintf(mapped, sizeof(mapped), "%s/builtin/default.map", root);
        return open(mapped, flags);
    }
    if (!strncmp(path, "/dev/input/event", 16)) {
        char *end;
        long node = strtol(path + 16, &end, 10);
        assert(!*end && node >= 0 && node < 32);
        assert(flags & O_NONBLOCK); assert(flags & O_CLOEXEC); assert(flags & O_NOFOLLOW);
        if (!fixture[node].present) { errno = ENOENT; return -1; }
        return 200 + (int)node;
    }
    return open(redirect(path, mapped, sizeof(mapped)), flags);
}
static char *mock_realpath(const char *path, char *output) {
    (void)output;
    if (!strcmp(path, "/etc/lilsmoke/input-maps/trusted.map")) return strdup("/os/files/test-map.bin");
    if (!strcmp(path, "/etc/lilsmoke/input-maps/untrusted.map")) return strdup("/persist/user.map");
    assert(!"unexpected symlink resolution"); return NULL;
}
static int mock_fstat(int fd, struct stat *st) {
    if (fd >= 200 && fd < 232) { memset(st, 0, sizeof(*st)); st->st_mode = S_IFCHR; return 0; }
    return fstat(fd, st);
}
static int mock_close(int fd) {
    if (fd >= 200 && fd < 232) { ++closes; fixture[fd - 200].grab = 0; return 0; }
    return close(fd);
}
static DIR *mock_opendir(const char *path) { char mapped[1024]; return opendir(redirect(path, mapped, sizeof(mapped))); }
static int mock_clock(clockid_t id, struct timespec *ts) {
    assert(id == CLOCK_MONOTONIC);
    ts->tv_sec = clock_ms / 1000; ts->tv_nsec = (clock_ms % 1000) * 1000000; return 0;
}
static int mock_ioctl(int fd, unsigned long request, ...) {
    assert(fd >= 200 && fd < 232);
    struct fixture *f = &fixture[fd - 200];
    va_list args; va_start(args, request);
    if (request == EVIOCGRAB) f->grab = va_arg(args, int);
    else {
        void *out = va_arg(args, void *);
        if (request == EVIOCGBIT(EV_KEY, M8_LS_KEY_BYTES)) memcpy(out, f->capabilities, sizeof(f->capabilities));
        else if (request == EVIOCGBIT(EV_ABS, sizeof(f->abs_bits))) memcpy(out, f->abs_bits, sizeof(f->abs_bits));
        else if (request == EVIOCGKEY(M8_LS_KEY_BYTES)) {
            if (f->key_error) { va_end(args); errno = EIO; return -1; }
            memcpy(out, f->keys, sizeof(f->keys));
        } else if (request == EVIOCGNAME(128)) snprintf(out, 128, "%s", f->name);
        else if (request == EVIOCGID) *(struct input_id *)out = (struct input_id){3, 0x1234, 0x5678, 0x0100};
        else {
            unsigned axis;
            for (axis = 0; axis <= ABS_MAX; ++axis) if (request == EVIOCGABS(axis)) break;
            assert(axis <= ABS_MAX);
            assert((f->abs_bits[axis / 8] >> (axis % 8)) & 1);
            *(struct input_absinfo *)out = f->axes[axis];
        }
    }
    va_end(args); return 0;
}
static ssize_t mock_read(int fd, void *out, size_t bytes) {
    assert(fd >= 200 && fd < 232);
    struct fixture *f = &fixture[fd - 200];
    ++f->read_calls;
    if (f->gone) { errno = ENODEV; return -1; }
    if (f->storm) {
        struct m8_ls_event event = {0, 0, EV_KEY, KEY_ENTER, 1};
        for (size_t i = 0; i < bytes / sizeof(event); ++i) memcpy((char *)out + i * sizeof(event), &event, sizeof(event));
        return bytes;
    }
    if (bytes > f->bytes - f->position) bytes = f->bytes - f->position;
    if (f->chunk && bytes > f->chunk) bytes = f->chunk;
    if (!bytes) { errno = EAGAIN; return -1; }
    memcpy(out, (char *)f->queue + f->position, bytes); f->position += bytes; return bytes;
}
void RARCH_LOG(const char *fmt, ...) { (void)fmt; }
void RARCH_WARN(const char *fmt, ...) { (void)fmt; }
bool input_autoconfigure_connect(const char *name, const char *display, const char *driver,
                                unsigned port, unsigned vid, unsigned pid) {
    (void)display; (void)vid; (void)pid;
    assert(name && !strcmp(driver, "lilsmoke") && port < 4); ++connects; return true;
}
bool input_autoconfigure_disconnect(unsigned port, const char *name) { assert(port < 4 && name); ++disconnects; return true; }

#define open mock_open
#define realpath mock_realpath
#define close mock_close
#define fstat mock_fstat
#define ioctl mock_ioctl
#define read mock_read
#define opendir mock_opendir
#define clock_gettime mock_clock
#include "../../package/retroarch-mips/m8_lilsmoke_input.c"
#undef open
#undef close
#undef fstat
#undef ioctl
#undef read
#undef opendir
#undef clock_gettime
#include "../gamepads/hotplug_file.h"

static void queue(unsigned node, unsigned type, unsigned code, int value) {
    struct fixture *f = &fixture[node];
    if (f->position == f->bytes) f->position = f->bytes = 0;
    assert(f->bytes + sizeof(struct m8_ls_event) <= sizeof(f->queue));
    struct m8_ls_event event = {123, 456, type, code, value};
    memcpy((char *)f->queue + f->bytes, &event, sizeof(event)); f->bytes += sizeof(event);
}
static void keyboard(unsigned node) {
    fixture[node].present = 1; fixture[node].name = "keyboard pad";
    bit(fixture[node].capabilities, KEY_UP); bit(fixture[node].capabilities, KEY_ENTER);
}
static void gamepad(unsigned node) {
    fixture[node].present = 1; fixture[node].name = "test gamepad";
    bit(fixture[node].capabilities, BTN_SOUTH); bit(fixture[node].capabilities, BTN_START);
    for (unsigned i = 0; i < 2; ++i) {
        bit(fixture[node].abs_bits, i); fixture[node].axes[i] = (struct input_absinfo){128, 0, 255, 0, 0, 0};
        bit(fixture[node].abs_bits, ABS_HAT0X + i);
        fixture[node].axes[ABS_HAT0X + i] = (struct input_absinfo){0, -1, 1, 0, 0, 0};
    }
}
static void write_map(const char *path, const char *text) {
    FILE *f = fopen(path, "w"); assert(f); assert(fputs(text, f) >= 0); assert(fclose(f) == 0);
}

int main(int argc, char **argv) {
    assert(argc == 2); root = argv[1];
    keyboard(0); keyboard(1); gamepad(2); gamepad(3);
    for (unsigned node = 2; node <= 3; ++node) {
        unsigned right_x = node == 2 ? ABS_RX : ABS_Z;
        unsigned right_y = node == 2 ? ABS_RY : ABS_RZ;
        bit(fixture[node].abs_bits, right_x); bit(fixture[node].abs_bits, right_y);
        fixture[node].axes[right_x] = fixture[node].axes[right_y] =
            (struct input_absinfo){128, 0, 255, 0, 0, 0};
    }
    fixture[3].name = "second gamepad";
    fixture[4].present = 1; fixture[4].name = "mouse"; bit(fixture[4].capabilities, BTN_LEFT);
    assert(lilsmoke_joypad.init(NULL));
    assert(!strcmp(lilsmoke_joypad.ident, "lilsmoke"));
    assert(query(0) && query(1) && !query(2));
    assert(fixture[0].grab && fixture[1].grab && fixture[2].grab && fixture[3].grab && !fixture[4].grab);
    assert(connects == 2);
    char path[1024];
    snprintf(path, sizeof(path), "%s/gamepads/%s.map", root, devices[2].identity);
    write_map(path, "1 304 0 b\n3 0 1 up\n");
    destroy(); assert(lilsmoke_joypad.init(NULL));
    assert(devices[2].keymap[BTN_SOUTH] == LS_BTN(0) && devices[2].absmap[ABS_X][1] == LS_BTN(4));
    assert(devices[3].keymap[BTN_SOUTH] == LS_BTN(8));
    assert(base_keys[KEY_LEFTCTRL] == LS_BTN(8));
    struct input_absinfo abs = {0, 0, 255, 0, 0, 0};
    assert(normalize(&abs) == -32767); abs.value = 128; assert(!normalize(&abs));
    abs.value = 255; assert(normalize(&abs) == 32767);
    abs.minimum = abs.maximum; assert(!normalize(&abs));
    abs = (struct input_absinfo){INT32_MAX, INT32_MIN, INT32_MAX, 0, 0, 0}; assert(normalize(&abs) == 32767);
    abs.value = INT32_MIN; assert(normalize(&abs) == -32767);

    /* D-Input exposes either RX/RY or Z/RZ; both must reach logical axes 2/3. */
    queue(2, EV_ABS, ABS_X, 0); queue(2, EV_ABS, ABS_Y, 255);
    queue(2, EV_ABS, ABS_RX, 255); queue(2, EV_ABS, ABS_RY, 0);
    queue(3, EV_ABS, ABS_Z, 0); queue(3, EV_ABS, ABS_RZ, 255);
    poll_input();
    assert(axis(0, AXIS_NEG(0)) == -32767 && axis(0, AXIS_POS(1)) == 32767);
    assert(axis(0, AXIS_POS(2)) == 32767 && axis(0, AXIS_NEG(3)) == -32767);
    assert(axis(1, AXIS_NEG(2)) == -32767 && axis(1, AXIS_POS(3)) == 32767);
    queue(2, EV_ABS, ABS_X, 128); queue(2, EV_ABS, ABS_Y, 128);
    queue(2, EV_ABS, ABS_RX, 128); queue(2, EV_ABS, ABS_RY, 128);
    queue(3, EV_ABS, ABS_Z, 128); queue(3, EV_ABS, ABS_RZ, 128);
    poll_input();
    for (unsigned port = 0; port < 2; ++port)
        for (unsigned index = 0; index < 4; ++index)
            assert(!axis(port, AXIS_POS(index)) && !axis(port, AXIS_NEG(index)));

    fixture[0].chunk = 7;
    queue(0, EV_KEY, KEY_LEFTCTRL, 1); queue(1, EV_KEY, KEY_LEFTCTRL, 1);
    poll_input(); assert(button(0, 8));
    queue(0, EV_KEY, KEY_LEFTCTRL, 0); poll_input(); assert(button(0, 8));
    queue(1, EV_KEY, KEY_LEFTCTRL, 0); poll_input(); assert(!button(0, 8));
    queue(0, EV_KEY, KEY_ENTER, 2); poll_input(); assert(button(0, 3));
    queue(0, EV_KEY, KEY_ENTER, 0); poll_input(); assert(!button(0, 3));
    queue(2, EV_ABS, ABS_HAT0X, -1); queue(3, EV_KEY, BTN_SOUTH, 1);
    poll_input(); assert(button(0, 6) && button(1, 8));
    queue(2, EV_ABS, ABS_HAT0X, 0); queue(2, EV_ABS, ABS_HAT0Y, 1);
    queue(2, EV_ABS, ABS_X, 255); poll_input();
    assert(!button(0, 6) && button(0, 5) && axis(0, AXIS_POS(0)) == 32767 && !axis(0, AXIS_NEG(0)));
    queue(2, EV_SYN, SYN_DROPPED, 0); poll_input(); assert(!held(0) && !axis(0, AXIS_POS(0)));
    bit(fixture[2].keys, BTN_START);
    queue(2, EV_KEY, BTN_SOUTH, 1); queue(2, EV_SYN, SYN_REPORT, 0); poll_input();
    assert(button(0, 3) && !button(0, 8));

    input_bits_t bits;
    memset(&bits, 255, sizeof(bits)); get_buttons(99, &bits);
    assert(!bits.data[0] && !bits.data[7]);
    assert(!button(UINT_MAX, 0) && !button(0, NO_BTN) && !axis(UINT_MAX, AXIS_POS(0)));
    struct retro_keybind binds[16], automatic[16];
    memset(binds, 0, sizeof(binds)); memset(automatic, 0, sizeof(automatic));
    for (unsigned i = 0; i < 16; ++i) {
        binds[i].joykey = i; binds[i].joyaxis = AXIS_NONE;
        automatic[i].joykey = NO_BTN; automatic[i].joyaxis = AXIS_NONE;
    }
    rarch_joypad_info_t info = {automatic, 0.5f, 0};
    assert((uint16_t)state(&info, binds, 0) & LS_BTN(3));
    info.joy_idx = 99; assert(!state(&info, binds, 0));

    fixture[2].gone = 1; fixture[2].present = 0; poll_input();
    assert(query(0) && query(1) && !button(0, 3));
    gamepad(5); clock_ms = 1001; poll_input(); assert(devices[2].node == 5 && devices[2].port == 0 && devices[3].port == 1);
    fixture[0].storm = 1; fixture[0].read_calls = 0; poll_input(); assert(fixture[0].read_calls == 8);
    fixture[0].storm = 0;

    snprintf(path, sizeof(path), "%s/override.map", root);
    write_map(path, "# override\nevdev 29 b\nevdev 99999999 a\nevdev 29junk a\nevdev 103 home\n");
    assert(load_map(path, NULL) == 2 && base_keys[KEY_LEFTCTRL] == LS_BTN(0) && base_keys[KEY_UP] == LS_BTN(16));
    struct m8_ls_device d = {0}; memcpy(d.keymap, base_keys, sizeof(base_keys));
    write_map(path, "1 304 0 b\n3 0 1 x\n");
    assert(load_map(path, &d) == 2 && d.keymap[304] == LS_BTN(0) && d.absmap[0][1] == LS_BTN(9));
    write_map(path, "1 304 0 y\n3 999999 1 x\n");
    assert(!load_map(path, &d) && d.keymap[304] == LS_BTN(0));
    FILE *trusted = map_open("/etc/lilsmoke/input-maps/trusted.map");
    assert(trusted); fclose(trusted);
    assert(!map_open("/etc/lilsmoke/input-maps/untrusted.map"));
    destroy(); assert(!query(0) && !query(1));
    for (unsigned i = 0; i < 32; ++i) assert(!fixture[i].grab);
    assert(disconnects == 4);
    /* Missing uinput must still expose separate gamepad ports through IPC. */
    for (unsigned i = 0; i < 32; ++i) fixture[i].present = 0;
    snprintf(path, sizeof(path), "%s/usb-state.bin", root);
    struct ls_gp_state usb = {0};
    usb.magic = LS_GP_MAGIC; usb.abi = 1; usb.millis = clock_ms;
    usb.pads[0].connected = usb.pads[1].connected = 1;
    strcpy(usb.pads[0].name, "Fixture XInput"); strcpy(usb.pads[1].name, "Fixture PlayStation");
    usb.pads[0].buttons = 1U << LS_GP_X; usb.pads[1].buttons = 1U << LS_GP_A;
    usb.pads[0].axes[0] = 16384; usb.pads[0].axes[4] = 32767;
    usb.pads[0].axes[1] = -18000; usb.pads[0].axes[2] = -22000; usb.pads[0].axes[3] = 24000;
    FILE *packet = fopen(path, "wb"); assert(packet);
    assert(fwrite(&usb, 1, sizeof(usb), packet) == sizeof(usb)); fclose(packet);
    setenv("LILSMOKE_GAMEPAD_STATE", path, 1);
    init(NULL); poll_input();
    assert(query(0) && query(1) && button(0, 9) && button(1, 8) && button(0, 12));
    assert(axis(0, AXIS_POS(0)) > 12000);
    assert(axis(0, AXIS_NEG(1)) < -14000 && axis(0, AXIS_NEG(2)) < -18000);
    assert(axis(0, AXIS_POS(3)) > 20000);
    clock_ms += 1000; poll_input();
    assert(!query(0) && !query(1) && !button(0, 9));
    destroy();
    struct gp_fixture shared; gp_setup(&shared);
    gp_pad(&shared, 0, 0x045e, 0x028e, 7);
    gp_pad(&shared, 1, 0x0079, 0x181c, 8);
    shared.packet.pads[0].buttons = (1U << LS_GP_START) | (1U << LS_GP_X);
    shared.packet.pads[0].axes[0] = 32767; shared.packet.pads[0].axes[3] = -32768;
    shared.packet.pads[1].buttons = 1U << LS_GP_A;
    gp_write(&shared, clock_ms, 1); init(NULL); poll_input();
    assert(button(0, M8_LS_MENU) && button(1, 8));
    assert(axis(0, AXIS_POS(0)) == 32767 && axis(0, AXIS_NEG(3)) == -32767);
    for (unsigned tick = 0; tick < 150; ++tick) {
        clock_ms += 8; gp_write(&shared, clock_ms, 0); poll_input();
        assert(button(0, M8_LS_MENU) && button(1, 8));
    }
    unsigned before = disconnects;
    gp_pad(&shared, 0, 0x1997, 0x2535, 7);
    shared.packet.pads[0].buttons = (1U << LS_GP_START) | (1U << LS_GP_BACK);
    gp_write(&shared, clock_ms, 0); poll_input();
    assert(button(0, M8_LS_EXIT) && !button(0, M8_LS_MENU) && disconnects == before + 1);
    before = disconnects; clock_ms += 100; gp_write(&shared, clock_ms, 1); poll_input();
    assert(button(0, M8_LS_EXIT) && disconnects == before + 2);
    shared.packet.pads[0].connected = 0; gp_write(&shared, clock_ms, 0); poll_input();
    assert(!query(0) && query(1) && button(1, 8));
    clock_ms += 501; poll_input(); assert(!query(1) && !held(1));
    clock_ms += 100; gp_write(&shared, clock_ms, 1);
    destroy(); init(NULL); poll_input(); assert(query(0) && button(0, 8));
    destroy(); gp_cleanup(&shared); unsetenv("LILSMOKE_GAMEPAD_STATE");
    printf("PASS: RA 1.19.1 callback ABI, 16-byte events/partial reads, keyboard union, canonical buttons, axes/hats/extremes, SYN_DROPPED, stable ports/hotplug, mouse rejection, grab cleanup, bounded storm, persistent maps/rejection. Device state=%zu bytes; no physical device opened or ioctl called.\n", sizeof(devices));
    return 0;
}
