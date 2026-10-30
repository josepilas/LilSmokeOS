/* GPL-3.0-or-later. Native bounded evdev RetroArch joypad; no external mapper. */
#define _GNU_SOURCE
#include "m8_lilsmoke_input.h"
#include "input/input_driver.h"
#include "tasks/tasks_internal.h"
#include "verbosity.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include "gamepad-state.h"

#define LS_BIT(bits, code) (((bits)[(code) / 8] >> ((code) % 8)) & 1U)
#define LS_BTN(id) (UINT32_C(1) << (id))
static struct m8_ls_device devices[M8_LS_DEVICES];
static uint32_t base_keys[KEY_CNT];
static uint64_t next_scan;
static bool initialized;
static struct ls_gp_reader usb_reader = LS_GP_READER_INIT;

static uint64_t now_ms(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) < 0) return 0;
    return (uint64_t)ts.tv_sec * 1000U + (uint64_t)ts.tv_nsec / 1000000U;
}

static uint32_t action(const char *name, bool *valid) {
    static const struct { const char *name; int button; } actions[] = {
        {"b", 0}, {"y", 1}, {"select", 2}, {"start", 3},
        {"up", 4}, {"down", 5}, {"left", 6}, {"right", 7},
        {"a", 8}, {"x", 9}, {"l1", 10}, {"r1", 11},
        {"l2", 12}, {"r2", 13}, {"l3", 14}, {"r3", 15},
        {"home", 16}, {"menu", 16}, {"power", -1}, {"vol_up", -1}, {"vol_down", -1}
    };
    unsigned i;
    *valid = true;
    for (i = 0; i < sizeof(actions) / sizeof(actions[0]); ++i)
        if (!strcmp(name, actions[i].name)) return actions[i].button < 0 ? 0 : LS_BTN(actions[i].button);
    *valid = false;
    return 0;
}

static void defaults(void) {
    memset(base_keys, 0, sizeof(base_keys));
    base_keys[KEY_UP] = LS_BTN(4); base_keys[KEY_DOWN] = LS_BTN(5);
    base_keys[KEY_LEFT] = LS_BTN(6); base_keys[KEY_RIGHT] = LS_BTN(7);
    base_keys[KEY_LEFTCTRL] = LS_BTN(8); base_keys[KEY_LEFTALT] = LS_BTN(0);
    base_keys[KEY_SPACE] = LS_BTN(9); base_keys[KEY_LEFTSHIFT] = LS_BTN(1);
    base_keys[KEY_TAB] = LS_BTN(10); base_keys[KEY_BACKSPACE] = LS_BTN(11);
    base_keys[KEY_PAGEUP] = LS_BTN(12); base_keys[KEY_PAGEDOWN] = LS_BTN(13);
    base_keys[KEY_ENTER] = LS_BTN(3); base_keys[KEY_ESC] = LS_BTN(2);
    base_keys[KEY_HOME] = base_keys[KEY_END] = LS_BTN(M8_LS_MENU);
    base_keys[BTN_SOUTH] = base_keys[BTN_TRIGGER] = base_keys[BTN_0] = LS_BTN(8);
    base_keys[BTN_EAST] = base_keys[BTN_THUMB] = base_keys[BTN_1] = LS_BTN(0);
    base_keys[BTN_WEST] = base_keys[BTN_TOP] = base_keys[BTN_3] = LS_BTN(9);
    base_keys[BTN_NORTH] = base_keys[BTN_THUMB2] = base_keys[BTN_2] = LS_BTN(1);
    base_keys[BTN_TL] = base_keys[BTN_4] = LS_BTN(10);
    base_keys[BTN_TR] = base_keys[BTN_5] = LS_BTN(11);
    base_keys[BTN_TL2] = base_keys[BTN_6] = LS_BTN(12);
    base_keys[BTN_TR2] = base_keys[BTN_7] = LS_BTN(13);
    base_keys[BTN_SELECT] = base_keys[BTN_8] = LS_BTN(2);
    base_keys[BTN_START] = base_keys[BTN_9] = LS_BTN(3);
    base_keys[BTN_THUMBL] = LS_BTN(14); base_keys[BTN_THUMBR] = LS_BTN(15);
    base_keys[BTN_MODE] = LS_BTN(M8_LS_MENU);
    base_keys[BTN_DPAD_UP] = LS_BTN(4); base_keys[BTN_DPAD_DOWN] = LS_BTN(5);
    base_keys[BTN_DPAD_LEFT] = LS_BTN(6); base_keys[BTN_DPAD_RIGHT] = LS_BTN(7);
}

static FILE *map_open(const char *path) {
    struct stat st;
    int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0 && errno == ELOOP && !strncmp(path, "/etc/lilsmoke/input-maps/",
                                          sizeof("/etc/lilsmoke/input-maps/") - 1)) {
        /* The stock-kernel bring-up uses trusted file-backed OS objects. */
        char *resolved = realpath(path, NULL);
        if (resolved && !strncmp(resolved, "/os/files/", 10))
            fd = open(resolved, O_RDONLY | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW);
        free(resolved);
    }
    if (fd < 0) return NULL;
    if (fstat(fd, &st) < 0 || !S_ISREG(st.st_mode) || st.st_size > 32768) { close(fd); return NULL; }
    FILE *file = fdopen(fd, "r");
    if (!file) close(fd);
    return file;
}

static bool decimal(const char *text, unsigned limit, unsigned *value) {
    char *end;
    long number;
    errno = 0;
    number = strtol(text, &end, 10);
    if (!*text || *end || errno || number < 0 || (unsigned long)number > limit) return false;
    *value = (unsigned)number; return true;
}

static int load_map(const char *path, struct m8_ls_device *device) {
    FILE *file = map_open(path);
    char line[256], source[32], number[32], direction[32], name[32], extra;
    unsigned count = 0, rows = 0, code, type;
    uint32_t keys[KEY_CNT], axes[ABS_CNT][2];
    bool valid, complete = true;
    if (!file) return 0;
    memcpy(keys, device ? device->keymap : base_keys, sizeof(keys));
    if (device) memcpy(axes, device->absmap, sizeof(axes));
    while (fgets(line, sizeof(line), file)) {
        uint32_t mask;
        char *start = line;
        if (++rows > 512 || (!strchr(line, '\n') && !feof(file))) { complete = false; break; }
        while (*start == ' ' || *start == '\t') ++start;
        if (*start == '#' || *start == '\n' || !*start) continue;
        if (!device) {
            if (sscanf(start, "%31s %31s %31s", source, number, name) != 3 ||
                strcmp(source, "evdev") || !decimal(number, KEY_MAX, &code)) continue;
            mask = action(name, &valid);
            if (valid) { keys[code] = mask; ++count; }
        } else {
            int sign;
            if (sscanf(start, "%31s %31s %31s %31s %c", source, number, direction, name, &extra) != 4 ||
                !decimal(source, EV_MAX, &type) || !decimal(number, type == EV_KEY ? KEY_MAX : ABS_MAX, &code)) { complete = false; break; }
            mask = action(name, &valid);
            if (!valid) { complete = false; break; }
            if (type == EV_KEY && !strcmp(direction, "0")) keys[code] = mask;
            else if (type == EV_ABS && ((sign = !strcmp(direction, "1")) || !strcmp(direction, "-1")))
                axes[code][sign] = mask;
            else { complete = false; break; }
            ++count;
        }
    }
    if (ferror(file)) complete = false;
    fclose(file);
    if (!complete) { RARCH_WARN("[lilsmoke]: rejected incomplete map %s\n", path); return 0; }
    memcpy(device ? device->keymap : base_keys, keys, sizeof(keys));
    if (device) memcpy(device->absmap, axes, sizeof(axes));
    if (count) RARCH_LOG("[lilsmoke]: map %s bindings=%u\n", path, count);
    return (int)count;
}

static int name_compare(const void *a, const void *b) { return strcmp(a, b); }
static void load_map_dir(const char *directory) {
    char names[32][128], path[256];
    unsigned count = 0, entries = 0, i;
    struct dirent *entry;
    DIR *dir;
    if (strlen(directory) > 100) return;
    dir = opendir(directory);
    if (!dir) return;
    while (entries++ < 256 && (entry = readdir(dir)) != NULL) {
        size_t size = strlen(entry->d_name);
        if (size < 5 || size >= sizeof(names[0]) || strcmp(entry->d_name + size - 4, ".map")) continue;
        if (count == 32) break;
        memcpy(names[count++], entry->d_name, size + 1);
    }
    closedir(dir);
    qsort(names, count, sizeof(names[0]), name_compare);
    for (i = 0; i < count; ++i) {
        snprintf(path, sizeof(path), "%.100s/%.127s", directory, names[i]); load_map(path, NULL);
    }
}

static int16_t normalize(const struct input_absinfo *axis) {
    int64_t minimum = axis->minimum, maximum = axis->maximum, value = axis->value;
    int64_t center, delta, span, flat;
    if (maximum <= minimum) return 0;
    if (value < minimum) value = minimum;
    if (value > maximum) value = maximum;
    center = (minimum + maximum) / 2;
    delta = value - center;
    span = delta < 0 ? center - minimum : maximum - center;
    flat = axis->flat > 0 ? axis->flat : (maximum - minimum) / 20;
    if (!span || (delta >= -flat && delta <= flat)) return 0;
    return (int16_t)(delta * 32767 / span);
}

static bool snapshot(struct m8_ls_device *d) {
    unsigned i;
    memset(d->keys, 0, sizeof(d->keys));
    if (ioctl(d->fd, EVIOCGKEY(sizeof(d->keys)), d->keys) < 0) return false;
    for (i = 0; i <= ABS_HAT3Y; ++i)
        if (d->present[i] && ioctl(d->fd, EVIOCGABS(i), &d->axes[i]) < 0) memset(&d->axes[i], 0, sizeof(d->axes[i]));
    return true;
}

static bool query(unsigned port) {
    unsigned i;
    if (port >= M8_LS_PORTS) return false;
    for (i = 0; i < M8_LS_DEVICES; ++i) if (devices[i].fd != -1 && (unsigned)devices[i].port == port) return true;
    return false;
}
static const char *pad_name(unsigned port) {
    unsigned i;
    if (port >= M8_LS_PORTS) return NULL;
    for (i = 0; i < M8_LS_DEVICES; ++i)
        if (devices[i].fd != -1 && (unsigned)devices[i].port == port && devices[i].gamepad) return devices[i].name;
    return query(port) ? "LilSmoke keyboard pad" : NULL;
}

static void disconnect(struct m8_ls_device *d) {
    unsigned port = (unsigned)d->port;
    char name[128];
    if (d->fd == -1) return;
    snprintf(name, sizeof(name), "%s", pad_name(port) ? pad_name(port) : "lilsmoke");
    if (d->fd >= 0) { (void)ioctl(d->fd, EVIOCGRAB, 0); (void)close(d->fd); }
    memset(d, 0, sizeof(*d)); d->fd = -1;
    if (!query(port)) input_autoconfigure_disconnect(port, name);
}

static void scan(void) {
    unsigned node, i;
    for (node = 0; node < M8_LS_NODES; ++node) {
        unsigned char bits[M8_LS_KEY_BYTES] = {0}, absbits[(ABS_MAX + 8) / 8] = {0};
        struct input_id id = {0};
        struct stat st;
        char path[256];
        int slot = -1, fd, port = 0, gamepad;
        bool known = false, was_connected;
        uint32_t signature = UINT32_C(2166136261);
        for (i = 0; i < M8_LS_DEVICES; ++i) {
            if (devices[i].fd >= 0 && devices[i].node == (int)node) known = true;
            if (devices[i].fd == -1 && slot < 0) slot = (int)i;
        }
        if (known || slot < 0) continue;
        snprintf(path, sizeof(path), "/dev/input/event%u", node);
        fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW);
        if (fd < 0) continue;
        if (fstat(fd, &st) < 0 || !S_ISCHR(st.st_mode) ||
            ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(bits)), bits) < 0) { close(fd); continue; }
        gamepad = LS_BIT(bits, BTN_SOUTH) || LS_BIT(bits, BTN_TRIGGER) || LS_BIT(bits, BTN_0) || LS_BIT(bits, BTN_DPAD_UP);
        if (!gamepad && !LS_BIT(bits, KEY_ENTER) && !LS_BIT(bits, KEY_UP) && !LS_BIT(bits, KEY_LEFTCTRL)) { close(fd); continue; }
        if (gamepad) {
            for (port = 0; port < M8_LS_PORTS; ++port) {
                for (i = 0; i < M8_LS_DEVICES; ++i)
                    if (devices[i].fd != -1 && devices[i].gamepad && devices[i].port == port) break;
                if (i == M8_LS_DEVICES) break;
            }
            if (port == M8_LS_PORTS) { close(fd); continue; }
        }
        if (ioctl(fd, EVIOCGRAB, 1) < 0) { close(fd); continue; }
        struct m8_ls_device *d = &devices[slot];
        was_connected = query((unsigned)port);
        memset(d, 0, sizeof(*d)); d->fd = fd; d->node = (int)node; d->port = port; d->gamepad = gamepad;
        memcpy(d->keymap, base_keys, sizeof(base_keys));
        if (ioctl(fd, EVIOCGNAME(sizeof(d->name)), d->name) < 0) strcpy(d->name, "unknown");
        d->name[sizeof(d->name) - 1] = '\0';
        (void)ioctl(fd, EVIOCGID, &id);
        (void)ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(absbits)), absbits);
        for (i = 0; i <= ABS_HAT3Y; ++i)
            d->present[i] = LS_BIT(absbits, i) && ioctl(fd, EVIOCGABS(i), &d->axes[i]) == 0;
        d->absmap[ABS_HAT0X][0] = LS_BTN(6); d->absmap[ABS_HAT0X][1] = LS_BTN(7);
        d->absmap[ABS_HAT0Y][0] = LS_BTN(4); d->absmap[ABS_HAT0Y][1] = LS_BTN(5);
        if (d->present[ABS_RX] && d->present[ABS_RY]) {
            d->absmap[ABS_Z][1] = LS_BTN(12); d->absmap[ABS_RZ][1] = LS_BTN(13);
        }
        d->absmap[ABS_HAT1X][1] = LS_BTN(11); d->absmap[ABS_HAT1Y][1] = LS_BTN(10);
        d->absmap[ABS_HAT2X][1] = LS_BTN(13); d->absmap[ABS_HAT2Y][1] = LS_BTN(12);
        for (i = 0; d->name[i]; ++i) signature = (signature ^ (unsigned char)d->name[i]) * UINT32_C(16777619);
        for (i = 0; i <= KEY_MAX; ++i) if (LS_BIT(bits, i)) signature = (signature ^ i) * UINT32_C(16777619);
        for (i = 0; i <= ABS_HAT3Y; ++i) if (d->present[i]) signature = (signature ^ i) * UINT32_C(16777619);
        snprintf(d->identity, sizeof(d->identity), "%04x-%04x-%04x-%04x-%08x", id.bustype, id.vendor, id.product, id.version, signature);
        if (gamepad) { snprintf(path, sizeof(path), "/persist/config/gamepads/%s.map", d->identity); load_map(path, d); }
        if (!snapshot(d)) { disconnect(d); continue; }
        if (!was_connected) input_autoconfigure_connect(pad_name((unsigned)port), NULL, "lilsmoke", (unsigned)port, id.vendor, id.product);
        RARCH_LOG("[lilsmoke]: event%u name=%s player=%d wire=%u id=%s\n", node, d->name, port + 1, (unsigned)sizeof(struct m8_ls_event), d->identity);
    }
}

static void consume(struct m8_ls_device *d, const struct m8_ls_event *event) {
    if (event->type == EV_SYN && event->code == SYN_DROPPED) { d->dropped = 1; return; }
    if (d->dropped) {
        if (event->type == EV_SYN && event->code == SYN_REPORT) {
            if (!snapshot(d)) disconnect(d); else d->dropped = 0;
        }
        return;
    }
    if (event->type == EV_KEY && event->code <= KEY_MAX && event->value >= 0 && event->value <= 2) {
        unsigned char mask = (unsigned char)(1U << (event->code % 8));
        if (event->value) d->keys[event->code / 8] |= mask;
        else d->keys[event->code / 8] &= (unsigned char)~mask;
    } else if (event->type == EV_ABS && event->code <= ABS_HAT3Y && d->present[event->code])
        d->axes[event->code].value = event->value;
}

static void poll_usb(void) {
    struct ls_gp_state packet;
    static const unsigned keys[] = {BTN_SOUTH, BTN_EAST, BTN_WEST, BTN_NORTH, BTN_SELECT,
        BTN_MODE, BTN_START, BTN_THUMBL, BTN_THUMBR, BTN_TL, BTN_TR,
        BTN_DPAD_UP, BTN_DPAD_DOWN, BTN_DPAD_LEFT, BTN_DPAD_RIGHT};
    static const unsigned axes[] = {ABS_X, ABS_Y, ABS_RX, ABS_RY, ABS_Z, ABS_RZ};
    ls_gp_read(&usb_reader, &packet);
    for (unsigned slot = 0; slot < LS_GP_SLOTS; ++slot) {
        const struct ls_gp_pad *pad = &packet.pads[slot];
        struct m8_ls_device *d = NULL;
        for (unsigned i = 0; i < M8_LS_DEVICES; ++i)
            if (devices[i].fd == -2 && devices[i].node == -(int)(100 + slot)) d = &devices[i];
        if (!pad->connected || pad->exported) { if (d) disconnect(d); continue; }
        if (d && (d->usb_generation != usb_reader.generation || d->usb_pid != packet.pid ||
                  d->usb_instance != pad->instance || d->usb_vendor != pad->vendor ||
                  d->usb_product != pad->product || d->usb_version != pad->version)) {
            disconnect(d); d = NULL;
        }
        if (!d) {
            for (unsigned i = 0; i < M8_LS_DEVICES; ++i) if (devices[i].fd == -1) { d = &devices[i]; break; }
            if (!d) continue;
            unsigned port;
            for (port = 0; port < M8_LS_PORTS; ++port) {
                unsigned i;
                for (i = 0; i < M8_LS_DEVICES; ++i)
                    if (devices[i].fd != -1 && devices[i].gamepad && devices[i].port == (int)port) break;
                if (i == M8_LS_DEVICES) break;
            }
            if (port == M8_LS_PORTS) continue;
            memset(d, 0, sizeof(*d)); d->fd = -2; d->port = port; d->node = -(int)(100 + slot); d->gamepad = 1;
            d->usb_generation = usb_reader.generation; d->usb_pid = packet.pid;
            d->usb_instance = pad->instance; d->usb_vendor = pad->vendor;
            d->usb_product = pad->product; d->usb_version = pad->version;
            memcpy(d->keymap, base_keys, sizeof(base_keys));
            memcpy(d->name, pad->name, sizeof(pad->name)); d->name[sizeof(pad->name)-1] = 0;
            snprintf(d->identity, sizeof(d->identity), "0003-%04x-%04x-%04x-hidapi", pad->vendor, pad->product, pad->version);
            for (unsigned i = 0; i < 6; ++i) {
                d->present[axes[i]] = 1; d->axes[axes[i]].minimum = i < 4 ? -32768 : 0;
                d->axes[axes[i]].maximum = 32767; d->axes[axes[i]].flat = i < 4 ? 3500 : 0;
            }
            d->absmap[ABS_Z][1] = LS_BTN(12); d->absmap[ABS_RZ][1] = LS_BTN(13);
            char path[256]; snprintf(path, sizeof(path), "/persist/config/gamepads/%s.map", d->identity); load_map(path, d);
            input_autoconfigure_connect(d->name, NULL, "lilsmoke", port, pad->vendor, pad->product);
            RARCH_LOG("[lilsmoke]: HIDAPI name=%s player=%u shared-state=1\n", d->name, port+1);
        }
        memset(d->keys, 0, sizeof(d->keys));
        for (unsigned i = 0; i < sizeof(keys)/sizeof(keys[0]); ++i)
            if (pad->buttons & (1U << i)) d->keys[keys[i]/8] |= 1U << (keys[i]%8);
        for (unsigned i = 0; i < 6; ++i) d->axes[axes[i]].value = pad->axes[i];
    }
}

static void poll_input(void) {
    unsigned i, batch;
    uint64_t now = now_ms();
    if (now >= next_scan) { scan(); next_scan = now + 1000; }
    poll_usb();
    for (i = 0; i < M8_LS_DEVICES; ++i) {
        struct m8_ls_device *d = &devices[i];
        for (batch = 0; d->fd >= 0 && batch < 8; ++batch) {
            unsigned char data[16 * sizeof(struct m8_ls_event)];
            ssize_t bytes = read(d->fd, data, sizeof(data));
            size_t at = 0;
            if (bytes < 0 && errno == EINTR) continue;
            if (bytes < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) break;
            if (bytes <= 0) { disconnect(d); break; }
            while (at < (size_t)bytes && d->fd >= 0) {
                size_t take = sizeof(struct m8_ls_event) - d->used;
                if (take > (size_t)bytes - at) take = (size_t)bytes - at;
                memcpy(d->partial + d->used, data + at, take); d->used += (unsigned)take; at += take;
                if (d->used == sizeof(struct m8_ls_event)) {
                    struct m8_ls_event event; memcpy(&event, d->partial, sizeof(event)); d->used = 0; consume(d, &event);
                }
            }
        }
    }
}

static uint32_t held(unsigned port) {
    uint32_t mask = 0;
    unsigned i, code;
    if (port >= M8_LS_PORTS) return 0;
    for (i = 0; i < M8_LS_DEVICES; ++i) {
        const struct m8_ls_device *d = &devices[i];
        if (d->fd == -1 || d->dropped || (unsigned)d->port != port) continue;
        for (code = 0; code <= KEY_MAX; ++code) if (LS_BIT(d->keys, code)) mask |= d->keymap[code];
        for (code = 0; code <= ABS_HAT3Y; ++code) {
            int16_t value;
            if (!d->present[code]) continue;
            value = normalize(&d->axes[code]);
            if (value < -8192) mask |= d->absmap[code][0];
            if (value > 8192) mask |= d->absmap[code][1];
        }
    }
    if ((mask & (LS_BTN(3) | LS_BTN(2))) == (LS_BTN(3) | LS_BTN(2)))
        return (mask & ~(LS_BTN(3) | LS_BTN(2))) | LS_BTN(M8_LS_EXIT);
    if ((mask & (LS_BTN(3) | LS_BTN(9))) == (LS_BTN(3) | LS_BTN(9)))
        return (mask & ~(LS_BTN(3) | LS_BTN(9))) | LS_BTN(M8_LS_MENU);
    return mask;
}

static int16_t axis(unsigned port, uint32_t joyaxis) {
    unsigned i, index = AXIS_NEG_GET(joyaxis), code;
    bool negative = index < 4;
    int16_t result = 0;
    if (!negative) index = AXIS_POS_GET(joyaxis);
    if (port >= M8_LS_PORTS || index >= 4) return 0;
    for (i = 0; i < M8_LS_DEVICES; ++i) {
        const struct m8_ls_device *d = &devices[i];
        int16_t value;
        if (d->fd == -1 || d->dropped || (unsigned)d->port != port) continue;
        code = index < 2 ? index : ((d->present[ABS_RX] && d->present[ABS_RY]) ? ABS_RX + index - 2 : ABS_Z + (index - 2) * 3);
        if (!d->present[code]) continue;
        value = normalize(&d->axes[code]);
        if (negative ? value < result : value > result) result = value;
    }
    return result;
}
static int32_t button(unsigned port, uint16_t key) { return key <= M8_LS_EXIT && (held(port) & LS_BTN(key)) ? 1 : 0; }
static void get_buttons(unsigned port, input_bits_t *state) { BIT256_CLEAR_ALL_PTR(state); state->data[0] = held(port); }
static int16_t state(rarch_joypad_info_t *info, const struct retro_keybind *binds, unsigned port) {
    unsigned i;
    uint16_t result = 0;
    uint32_t buttons;
    (void)port;
    if (!info || !binds || !info->auto_binds || info->joy_idx >= M8_LS_PORTS) return 0;
    buttons = held(info->joy_idx);
    for (i = 0; i < RARCH_FIRST_CUSTOM_BIND && i < 16; ++i) {
        uint16_t key = binds[i].joykey != NO_BTN ? binds[i].joykey : info->auto_binds[i].joykey;
        uint32_t joyaxis = binds[i].joyaxis != AXIS_NONE ? binds[i].joyaxis : info->auto_binds[i].joyaxis;
        int value = axis(info->joy_idx, joyaxis);
        if ((key <= M8_LS_MENU && (buttons & LS_BTN(key))) ||
            (joyaxis != AXIS_NONE && (float)abs(value) / 32768.0f > info->axis_threshold)) result |= (uint16_t)(1U << i);
    }
    return (int16_t)result;
}
static void destroy(void) {
    unsigned i;
    if (!initialized) return;
    for (i = 0; i < M8_LS_DEVICES; ++i) disconnect(&devices[i]);
    ls_gp_close(&usb_reader);
    initialized = false;
}
static void *init(void *data) {
    unsigned i;
    (void)data;
    destroy(); defaults();
    load_map_dir("/etc/lilsmoke/input-maps"); load_map_dir("/persist/config/input-maps");
    for (i = 0; i < M8_LS_DEVICES; ++i) devices[i].fd = -1;
    initialized = true; scan(); next_scan = now_ms() + 1000;
    RARCH_LOG("[lilsmoke]: native evdev active; canonical buttons=0..16 ports=4\n");
    return (void*)-1;
}

input_device_driver_t lilsmoke_joypad = {
    init, query, destroy, button, state, get_buttons, axis, poll_input,
    NULL, NULL, pad_name, "lilsmoke"
};
