/* SPDX-License-Identifier: MPL-2.0
 * Copyright (c) 2026 Jose Pilas.
 */
#include <lilsmoke/lifecycle.h>
#include <assert.h>
#include <stdio.h>

static void combos(void) {
    LS_Lifecycle s;
    ls_lifecycle_init(&s, 1500);
    ls_lifecycle_update(&s, LS_BUTTON_START, 100);
    ls_lifecycle_update(&s, LS_BUTTON_SELECT, 2000);
    assert(!s.combo_active && !s.exit_requested);
    /* Aggregate snapshots, not simultaneous pressed edges or one-pad identity. */
    ls_lifecycle_update(&s, LS_BUTTON_START, 2100);
    ls_lifecycle_update(&s, LS_EXIT_COMBO, 2200);
    ls_lifecycle_update(&s, LS_EXIT_COMBO, 3699);
    assert(s.combo_held_ms == 1499 && !s.exit_requested);
    ls_lifecycle_update(&s, LS_EXIT_COMBO, 3700);
    assert(ls_lifecycle_take_exit(&s) == LS_EXIT_HELD_COMBO);
    ls_lifecycle_update(&s, LS_EXIT_COMBO, 9000);
    assert(!s.exit_requested && s.combo_held_ms == 1500);
    ls_lifecycle_update(&s, LS_BUTTON_START, 9001);
    assert(!s.combo_active && !s.combo_latched && !s.combo_held_ms);
    ls_lifecycle_update(&s, LS_EXIT_COMBO, 9100);
    ls_lifecycle_update(&s, LS_EXIT_COMBO, 10599);
    assert(!s.exit_requested);
    ls_lifecycle_cancel_combo(&s); /* unplug even if aggregate is restored */
    ls_lifecycle_update(&s, LS_EXIT_COMBO, 10600);
    ls_lifecycle_update(&s, LS_EXIT_COMBO, 12099);
    assert(!s.exit_requested);
    ls_lifecycle_focus(&s, 0);
    ls_lifecycle_update(&s, LS_EXIT_COMBO, 13000);
    assert(!s.combo_active);
    ls_lifecycle_focus(&s, 1);
    ls_lifecycle_update(&s, LS_EXIT_COMBO, 14000);
    ls_lifecycle_update(&s, LS_EXIT_COMBO, 15500);
    ls_lifecycle_request_exit(&s, LS_EXIT_SDL_QUIT);
    assert(ls_lifecycle_take_exit(&s) == (LS_EXIT_HELD_COMBO | LS_EXIT_SDL_QUIT));
    ls_lifecycle_init(&s, 50);
    ls_lifecycle_update(&s, LS_EXIT_COMBO, UINT32_MAX - 20u);
    ls_lifecycle_update(&s, LS_EXIT_COMBO, 29);
    assert(s.exit_requested == LS_EXIT_HELD_COMBO);
    ls_lifecycle_init(&s, 50);
    ls_lifecycle_update(&s, LS_EXIT_COMBO, 100);
    ls_lifecycle_update(&s, LS_EXIT_COMBO, 99);
    assert(!s.exit_requested && !s.combo_held_ms);
    ls_lifecycle_init(&s, 0);
    ls_lifecycle_update(&s, LS_EXIT_COMBO, 1);
    ls_lifecycle_update(&s, LS_EXIT_COMBO, 10000);
    assert(!s.combo_active && !s.exit_requested);
    ls_lifecycle_init(&s, UINT32_MAX);
    assert(s.combo_delay_ms == LS_TIME_HALF - 1u);
}
static void pacing(void) {
    LS_FrameClock c;
    assert(sizeof c <= 256);
    assert(!ls_frame_init(&c, 33, 100));
    assert(ls_frame_init(&c, 0, 0) == -1 && c.period_ms == 33);
    assert(ls_frame_init(&c, 1001, 0) == -1 && c.last_ms == 100);
    assert(ls_frame_wait_ms(&c, 110) == 23);
    ls_frame_finish(&c, 110, 134); /* one ms oversleep preserves phase */
    assert(c.deadline_ms == 166 && c.stats.delta_ms == 34 && c.stats.work_ms == 10);
    assert(c.stats.late_frames == 1 && !c.stats.missed_frames);
    assert(ls_frame_wait_ms(&c, 140) == 26);
    assert(!ls_frame_wait_ms(&c, 200));
    ls_frame_finish(&c, 200, 200);
    assert(c.deadline_ms == 232 && c.stats.missed_frames == 1);
    ls_frame_finish(&c, 4000, 4000);
    assert(c.stats.delta_ms == 250 && c.stats.work_ms == 250 && c.stats.late_ms == 250);
    assert(c.stats.clamped_frames == 1 && c.stats.max_delta_ms == 250);
    assert(c.deadline_ms > 4000 && c.deadline_ms <= 4033);
    ls_frame_finish(&c, 3999, 3999);
    assert(c.stats.clock_resets == 1 && c.deadline_ms == 4032 && !c.stats.delta_ms);
    assert(!ls_frame_wait_ms(&c, 3000)); /* backwards clock cannot sleep seconds */
    assert(!ls_frame_init(&c, 16, UINT32_MAX - 7u));
    assert(c.deadline_ms == 8 && ls_frame_wait_ms(&c, UINT32_MAX - 2u) == 11);
    ls_frame_finish(&c, 3, 8);
    assert(c.stats.delta_ms == 16 && c.stats.work_ms == 11 && c.deadline_ms == 24);
    assert(!ls_frame_init(&c, 10, UINT32_MAX - 5u));
    ls_frame_finish(&c, 31, 31);
    assert(c.deadline_ms == 34 && c.stats.missed_frames == 2 && c.stats.late_ms == 27);
    ls_frame_finish(&c, UINT32_C(0x80000020), UINT32_C(0x80000020));
    assert(c.stats.clock_resets == 1 && !c.stats.delta_ms);
    assert(!ls_frame_init(&c, 10, 0));
    for (unsigned i = 1; i <= 200; ++i) ls_frame_finish(&c, i * 10u - 2u, i * 10u);
    assert(c.stats.frames == 200 && c.stats.window_count == LS_FRAME_WINDOW);
    assert(c.stats.window_sum_ms == 640 && ls_frame_average_ms(&c) == 10);
    c.stats.frames = c.stats.late_frames = c.stats.missed_frames = UINT32_MAX;
    c.stats.clamped_frames = c.stats.clock_resets = UINT32_MAX;
    ls_frame_finish(&c, 6000, 6000);
    ls_frame_finish(&c, 5999, 5999);
    assert(c.stats.frames == UINT32_MAX && c.stats.late_frames == UINT32_MAX);
    assert(c.stats.missed_frames == UINT32_MAX && c.stats.clamped_frames == UINT32_MAX);
    assert(c.stats.clock_resets == UINT32_MAX && c.stats.window_sum_ms <= 16000);
    assert(ls_counter_add(UINT32_MAX - 1u, 2) == UINT32_MAX);
    assert(!ls_frame_init(&c, 1000, 0) && !ls_frame_average_ms(&c));
    ls_frame_finish(&c, 900, 1000);
    assert(c.stats.delta_ms == 250 && c.stats.clamped_frames == 1);
}
int main(void) {
    combos(); pacing();
    puts("PASS lifecycle aggregate/release/latch/focus/unplug/wrap; pacing phase/skip/clamp/window/saturation/reset");
    return 0;
}
