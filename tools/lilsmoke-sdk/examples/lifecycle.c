/* SPDX-License-Identifier: MPL-2.0
 * Copyright (c) 2026 Jose Pilas.
 */
#include <lilsmoke/sdl.h>
#include <stdio.h>
#include <stdlib.h>

/* An optional frame count makes the same example usable in headless QA. */
int main(int argc, char **argv) {
    LS_App app;
    LS_InputState input = {0};
    LS_FrameClock clock;
    unsigned limit = 0;
    if (argc > 2) return 2;
    if (argc == 2) {
        char *end;
        unsigned long value = strtoul(argv[1], &end, 10);
        if (*end || !value || value > 100000) return 2;
        limit = (unsigned)value;
    }
    if (ls_app_open(&app, "Lifecycle", 0)) return 1;
    (void)ls_frame_init(&clock, 33, SDL_GetTicks());
    while (!app.lifecycle.exit_requested) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) (void)ls_input(&app, &event);
        ls_app_poll(&app, &input);
        char line[80];
        ls_begin(&app, 0xf1f4f4);
        snprintf(line, sizeof line, "HELD %u / COMBO %u MS", input.held, app.lifecycle.combo_held_ms);
        ls_text(&app, line, 24, 24, 2, 0x20282b);
        snprintf(line, sizeof line, "FRAME %u / WORK %u / MISSED %u", clock.stats.delta_ms,
                 clock.stats.work_ms, clock.stats.missed_frames);
        ls_text(&app, line, 24, 64, 2, 0x008c89);
        ls_text(&app, app.lifecycle.focused ? "FOCUSED" : "FOCUS LOST", 24, 104, 2, 0x20282b);
        ls_present(&app);
        ls_frame_pace(&clock);
        if (limit && clock.stats.frames >= limit)
            ls_lifecycle_request_exit(&app.lifecycle, LS_EXIT_APPLICATION);
    }
    printf("frames=%u mean_ms=%u exit=%u\n", clock.stats.frames,
           ls_frame_average_ms(&clock), app.lifecycle.exit_requested);
    ls_app_close(&app);
    return 0;
}
