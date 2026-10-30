/* SPDX-License-Identifier: MPL-2.0
 * Copyright (c) 2026 Jose Pilas.
 * Source-level snapshots, not a hardware or OS binary ABI.
 */
#ifndef LILSMOKE_SDK_INPUT_H
#define LILSMOKE_SDK_INPUT_H
#include <stdint.h>

enum {
    LS_BUTTON_UP = 1u << 0, LS_BUTTON_DOWN = 1u << 1,
    LS_BUTTON_LEFT = 1u << 2, LS_BUTTON_RIGHT = 1u << 3,
    LS_BUTTON_ACTIVATE = 1u << 4, LS_BUTTON_BACK = 1u << 5,
    LS_BUTTON_MENU = 1u << 6,
    LS_BUTTON_START = 1u << 7, LS_BUTTON_SELECT = 1u << 8
};
#define LS_INPUT_PADS 8
#define LS_INPUT_AXES 6
typedef struct {
    int connected, mapped;
    int32_t id;
    uint32_t held, axes_available;
    /* Mapped SDL controller order: LX, LY, RX, RY, LT, RT.
     * Raw joysticks: backend axes 0..5, no semantic mapping promised.
     * Signed axes [-1,1], mapped triggers [0,1]; no synthetic digital axes. */
    float axes[LS_INPUT_AXES];
} LS_InputPad;
typedef struct {
    uint32_t held, pressed, released;
    LS_InputPad pads[LS_INPUT_PADS];
} LS_InputState;

/* Call once per frame with the aggregate held mask. Zero-initialize state.
 * Edges compare snapshots, so taps entirely between snapshots are not retained. */
static inline void ls_input_update(LS_InputState *state, uint32_t held) {
    state->pressed = held & ~state->held;
    state->released = state->held & ~held;
    state->held = held;
}
static inline float ls_axis_normalize(int16_t value) {
    return value < 0 ? (float)value / 32768.0f : (float)value / 32767.0f;
}
#endif
