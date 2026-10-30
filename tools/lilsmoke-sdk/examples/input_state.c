/* SPDX-License-Identifier: MPL-2.0
 * Copyright (c) 2026 Jose Pilas.
 */
#include <lilsmoke/sdl.h>
#include <stdio.h>

int main(void) {
    LS_App app;
    LS_InputState input = {0};
    if (ls_app_open(&app, "Input State", 0)) return 1;
    int running = 1;
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event))
            if (ls_input(&app, &event) == LS_QUIT) running = 0;
        ls_input_poll(&app, &input);
        if (input.pressed & LS_BUTTON_BACK) running = 0;
        if (input.pressed || input.released)
            printf("held=%u pressed=%u released=%u\n", input.held, input.pressed, input.released);
        ls_begin(&app, 0x182829);
        for (int i = 0; i < LS_INPUT_PADS; ++i)
            if (input.pads[i].axes_available & 3u)
                ls_fill(&app, 312 + (int)(input.pads[i].axes[0] * 180),
                        232 + (int)(input.pads[i].axes[1] * 180), 16, 16, 0x32d2b4);
        ls_present(&app);
        SDL_Delay(16);
    }
    ls_app_close(&app);
    return 0;
}
