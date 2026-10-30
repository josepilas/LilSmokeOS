/* SPDX-License-Identifier: MPL-2.0 */
#ifndef LILSMOKE_CHOCOLATE_DOOM_INPUT_H
#define LILSMOKE_CHOCOLATE_DOOM_INPUT_H
#include <stdint.h>

/* Opt-in native joy_key contract, not a face-button swap or an analog ABI.
   Source: Kryptikone316/chocolate-doom, sf3500/src/sf3500/sf3500_bridge.c.
   X is the strafe modifier; arrows turn/move, Y runs, B uses, L/R select
   weapons. This backend has no vertical look, nor native L2/R2 actions. */
static inline uint32_t chocolate_doom_controls(uint32_t raw,const int16_t right[2]) {
    if(right[0]<-14000)raw|=1u<<7;
    if(right[0]>14000)raw|=1u<<5;
    if(raw&(1u<<8))raw|=(1u<<12)|(1u<<7); /* L2: strafe left */
    if(raw&(1u<<9))raw|=(1u<<12)|(1u<<5); /* R2: strafe right */
    if(raw&(1u<<1))raw|=1u<<15; /* L3: run */
    if(raw&(1u<<2))raw|=1u<<14; /* R3: use/menu back */
    return raw;
}
#endif
