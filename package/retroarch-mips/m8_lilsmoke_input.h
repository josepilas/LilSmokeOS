#ifndef M8_LILSMOKE_INPUT_H
#define M8_LILSMOKE_INPUT_H
#include <linux/input.h>
#include <stdint.h>

#define M8_LS_DEVICES 8
#define M8_LS_PORTS 4
#define M8_LS_NODES 32
#define M8_LS_MENU 16
#define M8_LS_EXIT 17
#define M8_LS_KEY_BYTES ((KEY_MAX + 8) / 8)
/* Canonical buttons are libretro RetroPad IDs 0..15; Home/Menu is button 16.
 * Physical axes 0..3 are LX, LY, RX, RY. Hats are canonical D-pad buttons.
 * Existing UI maps: /etc/lilsmoke/input-maps, /persist/config/input-maps,
 * /persist/config/gamepads/<UI input_id/capability hash>.map. */
#if __SIZEOF_LONG__ == 4 || defined(M8_LILSMOKE_TEST_16)
struct m8_ls_event { uint32_t sec, usec; uint16_t type, code; int32_t value; };
_Static_assert(sizeof(struct m8_ls_event) == 16, "Linux 4.4 MIPS time32 wire ABI");
#else
#define m8_ls_event input_event
#endif

struct m8_ls_device {
    int fd, node, port, gamepad, dropped;
    uint64_t usb_generation;
    uint32_t usb_instance, usb_pid;
    uint16_t usb_vendor, usb_product, usb_version;
    unsigned used;
    unsigned char partial[sizeof(struct m8_ls_event)];
    unsigned char keys[M8_LS_KEY_BYTES], present[ABS_CNT];
    struct input_absinfo axes[ABS_CNT];
    uint32_t keymap[KEY_CNT], absmap[ABS_CNT][2];
    char name[128], identity[80];
};
_Static_assert(sizeof(struct m8_ls_device) * M8_LS_DEVICES < 65536, "bounded evdev state");
#endif
