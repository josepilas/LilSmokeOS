/* SPDX-License-Identifier: MPL-2.0 */
#ifndef LILSMOKE_TREEFROG_INPUT_MAP_H
#define LILSMOKE_TREEFROG_INPUT_MAP_H
#include "gamepad-state.h"
static inline uint32_t treefrog_pad(uint32_t buttons, const int16_t axes[6]) {
    static const unsigned from[] = {LS_GP_BACK, LS_GP_L3, LS_GP_R3, LS_GP_START,
        LS_GP_UP, LS_GP_RIGHT, LS_GP_DOWN, LS_GP_LEFT, LS_GP_LB, LS_GP_RB,
        LS_GP_X, LS_GP_A, LS_GP_B, LS_GP_Y};
    static const unsigned to[] = {0,1,2,3,4,5,6,7,10,11,12,13,14,15};
    uint32_t word=0;
    for (unsigned i=0; i<sizeof(from)/sizeof(from[0]); ++i)
        if (buttons & (UINT32_C(1)<<from[i])) word |= UINT32_C(1)<<to[i];
    if (buttons & (UINT32_C(1)<<LS_GP_GUIDE)) word |= UINT32_C(1)<<16;
    if (axes[0] < -14000) word |= 1u<<7;
    if (axes[0] > 14000) word |= 1u<<5;
    if (axes[1] < -14000) word |= 1u<<4;
    if (axes[1] > 14000) word |= 1u<<6;
    if (axes[4] > 14000) word |= 1u<<8;
    if (axes[5] > 14000) word |= 1u<<9;
    return word;
}
#endif
