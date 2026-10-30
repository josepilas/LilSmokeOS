/* SPDX-License-Identifier: MPL-2.0 */
#ifndef LILSMOKE_CLASSICUBE_INPUT_H
#define LILSMOKE_CLASSICUBE_INPUT_H
#include <stdint.h>

/* FroggyCraft's original digital controls, not an invented analog core ABI.
   Existing buttons and the left stick retain their original behavior. */
static inline uint32_t classicube_sticks(uint32_t raw, const int16_t right[2]) {
    if (right[0] < -14000) raw |= 1u << 15; /* Y: look left */
    if (right[0] > 14000) raw |= 1u << 13;  /* A: look right */
    if (right[1] < -14000) raw |= 1u << 12; /* X: look up */
    if (right[1] > 14000) raw |= 1u << 14;  /* B: look down */
    return raw;
}
#endif
