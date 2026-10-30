/* SPDX-License-Identifier: MPL-2.0
 * Copyright (c) 2026 Jose Pilas.
 * Original allocation-free source API. All times are monotonic milliseconds.
 */
#ifndef LILSMOKE_SDK_LIFECYCLE_H
#define LILSMOKE_SDK_LIFECYCLE_H
#include <stdint.h>
#include <string.h>
#include <lilsmoke/input.h>

#define LS_EXIT_COMBO (LS_BUTTON_START | LS_BUTTON_SELECT)
#define LS_FRAME_WINDOW 64u
#define LS_FRAME_MAX_DELTA_MS 250u
#define LS_TIME_HALF UINT32_C(0x80000000)
enum { LS_EXIT_NONE = 0, LS_EXIT_HELD_COMBO = 1, LS_EXIT_SDL_QUIT = 2,
       LS_EXIT_APPLICATION = 4 };

typedef struct {
    uint32_t exit_requested, combo_delay_ms, combo_started_ms, combo_held_ms;
    int focused, combo_active, combo_latched;
} LS_Lifecycle;

static inline void ls_lifecycle_init(LS_Lifecycle *s, uint32_t hold_ms) {
    memset(s, 0, sizeof *s);
    s->focused = 1;
    /* Zero disables the combo; ambiguous half-wrap durations are bounded. */
    s->combo_delay_ms = hold_ms >= LS_TIME_HALF ? LS_TIME_HALF - 1u : hold_ms;
}
static inline void ls_lifecycle_cancel_combo(LS_Lifecycle *s) {
    s->combo_active = s->combo_latched = 0;
    s->combo_held_ms = 0;
}
static inline void ls_lifecycle_focus(LS_Lifecycle *s, int focused) {
    s->focused = !!focused;
    ls_lifecycle_cancel_combo(s);
}
static inline void ls_lifecycle_request_exit(LS_Lifecycle *s, uint32_t reason) {
    s->exit_requested |= reason;
}
/* Taking a request does not re-arm a still-held combo. The caller owns policy,
 * saving and teardown; this API never emits keys, signals or SDL events. */
static inline uint32_t ls_lifecycle_take_exit(LS_Lifecycle *s) {
    uint32_t reason = s->exit_requested;
    s->exit_requested = 0;
    return reason;
}
static inline void ls_lifecycle_update(LS_Lifecycle *s, uint32_t held, uint32_t now) {
    if (!s->focused || !s->combo_delay_ms || (held & LS_EXIT_COMBO) != LS_EXIT_COMBO) {
        ls_lifecycle_cancel_combo(s);
        return;
    }
    if (!s->combo_active) {
        s->combo_started_ms = now;
        s->combo_active = 1;
    }
    uint32_t elapsed = now - s->combo_started_ms;
    if (elapsed >= LS_TIME_HALF) {
        s->combo_started_ms = now;
        elapsed = 0;
    }
    s->combo_held_ms = elapsed < s->combo_delay_ms ? elapsed : s->combo_delay_ms;
    if (!s->combo_latched && elapsed >= s->combo_delay_ms) {
        s->combo_latched = 1;
        ls_lifecycle_request_exit(s, LS_EXIT_HELD_COMBO);
    }
}

typedef struct {
    uint32_t frames, late_frames, missed_frames, clamped_frames, clock_resets;
    uint32_t delta_ms, work_ms, late_ms, max_delta_ms, window_sum_ms;
    uint16_t samples[LS_FRAME_WINDOW];
    uint8_t window_count, window_index;
} LS_FrameStats;
typedef struct {
    uint32_t period_ms, last_ms, deadline_ms;
    LS_FrameStats stats;
} LS_FrameClock;

static inline uint32_t ls_counter_add(uint32_t value, uint32_t amount) {
    return amount > UINT32_MAX - value ? UINT32_MAX : value + amount;
}
/* Returns -1 for an invalid period, leaving the clock untouched. Integer periods
 * of 1..1000 ms are intentional: no floating point or busy-waiting on MIPS. */
static inline int ls_frame_init(LS_FrameClock *c, uint32_t period_ms, uint32_t now) {
    if (!period_ms || period_ms > 1000u) return -1;
    memset(c, 0, sizeof *c);
    c->period_ms = period_ms;
    c->last_ms = now;
    c->deadline_ms = now + period_ms;
    return 0;
}
static inline uint32_t ls_frame_wait_ms(const LS_FrameClock *c, uint32_t now) {
    uint32_t remaining = c->deadline_ms - now;
    /* A backwards/discontinuous clock must never produce a giant sleep. */
    return remaining < LS_TIME_HALF && remaining <= c->period_ms ? remaining : 0;
}
static inline uint32_t ls_frame_average_ms(const LS_FrameClock *c) {
    return c->stats.window_count ? c->stats.window_sum_ms / c->stats.window_count : 0;
}
/* Once per frame AFTER waiting. work_end is sampled before the wait. Modular
 * subtraction handles a 32-bit clock wrap; gaps must be less than 2^31 ms. */
static inline void ls_frame_finish(LS_FrameClock *c, uint32_t work_end, uint32_t now) {
    LS_FrameStats *s = &c->stats;
    uint32_t delta = now - c->last_ms, work = work_end - c->last_ms;
    uint32_t late = now - c->deadline_ms;
    s->frames = ls_counter_add(s->frames, 1);
    if (delta >= LS_TIME_HALF || work > delta) {
        s->clock_resets = ls_counter_add(s->clock_resets, 1);
        delta = work = late = 0;
        c->deadline_ms = now + c->period_ms;
    } else {
        if (late >= LS_TIME_HALF) late = 0;
        uint32_t missed = late / c->period_ms;
        if (late) s->late_frames = ls_counter_add(s->late_frames, 1);
        s->missed_frames = ls_counter_add(s->missed_frames, missed);
        /* Skip missed slots in O(1), preserving phase without catch-up bursts. */
        c->deadline_ms += (missed + 1u) * c->period_ms;
    }
    if (delta > LS_FRAME_MAX_DELTA_MS) s->clamped_frames = ls_counter_add(s->clamped_frames, 1);
    s->delta_ms = delta > LS_FRAME_MAX_DELTA_MS ? LS_FRAME_MAX_DELTA_MS : delta;
    s->work_ms = work > LS_FRAME_MAX_DELTA_MS ? LS_FRAME_MAX_DELTA_MS : work;
    s->late_ms = late > LS_FRAME_MAX_DELTA_MS ? LS_FRAME_MAX_DELTA_MS : late;
    if (s->delta_ms > s->max_delta_ms) s->max_delta_ms = s->delta_ms;
    s->window_sum_ms -= s->samples[s->window_index];
    s->samples[s->window_index] = (uint16_t)s->delta_ms;
    s->window_sum_ms += s->delta_ms;
    s->window_index = (uint8_t)((s->window_index + 1u) % LS_FRAME_WINDOW);
    if (s->window_count < LS_FRAME_WINDOW) ++s->window_count;
    c->last_ms = now;
}
#endif
