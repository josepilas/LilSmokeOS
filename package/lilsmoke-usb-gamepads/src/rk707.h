#ifndef LILSMOKE_RK707_H
#define LILSMOKE_RK707_H
#include "gamepad-state.h"

static inline int rk707_axis(unsigned value) {
    int offset = (int)value - 128;
    return offset < 0 ? offset * 256 : offset * 32767 / 127;
}
/* Captured RK707 0079:181c reports, PS mode, no report-ID byte. */
static inline int rk707_decode(const unsigned char *bytes, size_t size, struct ls_gp_pad *pad) {
    if (size != 27 || (bytes[2] > 7 && bytes[2] != 15)) return 0;
    const unsigned raw = bytes[0] | ((unsigned)bytes[1] << 8);
    static const int bit_to_button[12] = {3,1,0,2,9,10,-1,-1,4,6,8,7};
    pad->buttons = 0;
    for (unsigned i = 0; i < 12; ++i)
        if ((raw & (1U << i)) && bit_to_button[i] >= 0) pad->buttons |= 1U << bit_to_button[i];
    const unsigned hat = bytes[2];
    if (hat == 0 || hat == 1 || hat == 7) pad->buttons |= 1U << 11;
    if (hat == 1 || hat == 2 || hat == 3) pad->buttons |= 1U << 14;
    if (hat == 3 || hat == 4 || hat == 5) pad->buttons |= 1U << 12;
    if (hat == 5 || hat == 6 || hat == 7) pad->buttons |= 1U << 13;
    for (unsigned i = 0; i < 4; ++i) pad->axes[i] = rk707_axis(bytes[3+i]);
    pad->axes[4] = raw & 0x40 ? 32767 : 0;
    pad->axes[5] = raw & 0x80 ? 32767 : 0;
    return 1;
}
#endif
