#define _GNU_SOURCE
#include <SDL.h>
#include <SDL_hidapi.h>
#include <assert.h>
static struct SDL_hid_device_info info[3];
static unsigned inventory, pending[2], failed[2], opened[2], closed[2];
static struct SDL_hid_device_info *mock_enumerate(unsigned short vendor, unsigned short product) {
    assert(vendor == 0x0079 && product == 0x181c);
    return inventory ? &info[0] : NULL;
}
static void mock_free(struct SDL_hid_device_info *list) { (void)list; }
static SDL_hid_device *mock_open(const char *path, int exclusive) {
    assert(!exclusive);
    unsigned index = !strcmp(path, "receiver-b");
    assert(!strcmp(path, index ? "receiver-b" : "receiver-a"));
    ++opened[index]; return (SDL_hid_device *)(uintptr_t)(index + 1);
}
static void mock_close(SDL_hid_device *device) { ++closed[(uintptr_t)device - 1]; }
static int mock_nonblocking(SDL_hid_device *device, int value) { (void)device; assert(value == 1); return 0; }
static int mock_read(SDL_hid_device *device, unsigned char *out, size_t size, int timeout) {
    unsigned index = (uintptr_t)device - 1;
    assert(size >= 27 && !timeout);
    if (failed[index]) return -1;
    if (!pending[index]) return 0;
    --pending[index]; memset(out, 0, 27);
    out[0] = 8; out[2] = 15;
    out[3] = 255; out[4] = out[5] = out[6] = 128;
    return 27;
}
#define SDL_hid_enumerate mock_enumerate
#define SDL_hid_free_enumeration mock_free
#define SDL_hid_open_path mock_open
#define SDL_hid_close mock_close
#define SDL_hid_set_nonblocking mock_nonblocking
#define SDL_hid_read_timeout mock_read
#define main unused_daemon_main
#include "../../package/lilsmoke-usb-gamepads/src/usb-gamepads.c"
#undef main

int main(void) {
    struct pad pads[LS_GP_SLOTS] = {0};
    for (unsigned i = 0; i < LS_GP_SLOTS; ++i) pads[i].output = -1;
    info[0].path = "receiver-a"; info[0].interface_number = 0;
    info[0].release_number = 0x103; info[0].next = &info[1];
    info[1].path = "receiver-b"; info[1].interface_number = 0;
    info[1].next = &info[2];
    info[2].path = "keyboard-never-open"; info[2].interface_number = 1;
    inventory = 1; scan_raw(pads);
    assert(opened[0] == 1 && opened[1] == 1);
    assert(pads[0].raw && pads[1].raw && !pads[2].raw);
    assert(pads[0].previous.instance != pads[1].previous.instance);
    struct ls_gp_pad data = {0};
    poll_raw(&pads[0], &data, 0); assert(!data.connected);
    pending[0] = 1; poll_raw(&pads[0], &data, 0);
    assert(data.connected && data.buttons == (1U << LS_GP_X) && data.axes[0] == 32767);
    /* 150 idle ticks exceed 1000ms in the production 8ms loop. */
    for (unsigned i = 0; i < 150; ++i) {
        poll_raw(&pads[0], &data, 0);
        assert(data.connected && data.buttons && data.axes[0] == 32767);
    }
    uint32_t first_instance = data.instance;
    failed[0] = 1; memset(&data, 0, sizeof(data)); poll_raw(&pads[0], &data, 0);
    assert(!data.connected && !data.buttons && !pads[0].raw && closed[0] == 1);
    failed[0] = 0; scan_raw(pads);
    assert(pads[0].raw && opened[0] == 2 && opened[1] == 1);
    assert(pads[0].previous.instance != first_instance);
    pending[0] = 1; poll_raw(&pads[0], &data, 0); assert(data.connected);
    inventory = 0; scan_raw(pads);
    assert(!pads[0].raw && !pads[1].raw && !pads[0].previous.buttons);
    inventory = 1; scan_raw(pads);
    assert(pads[0].raw && pads[1].raw);
    for (unsigned i = 0; i < LS_GP_SLOTS; ++i) disconnect(&pads[i]);
    puts("PASS daemon PS raw path: validated connect, two receivers, idle >1000ms, read-error/retry, generation, removal, keyboard isolation");
}
