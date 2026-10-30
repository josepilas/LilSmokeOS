/* SPDX-License-Identifier: MPL-2.0
 * Copyright (c) 2026 Jose Pilas.
 * Synthetic SDL devices qualify API state, not physical controllers.
 */
#define _POSIX_C_SOURCE 200809L
#include <lilsmoke/sdl.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void drain(LS_App *a) {
    SDL_Event e;
    SDL_JoystickUpdate();
    while (SDL_PollEvent(&e)) (void)ls_input(a, &e);
}
static void key(LS_App *a, SDL_Keycode code, int down) {
    SDL_Event e;
    memset(&e, 0, sizeof e);
    e.type = down ? SDL_KEYDOWN : SDL_KEYUP;
    e.key.keysym.sym = code;
    (void)ls_input(a, &e);
}
static void focus(LS_App *a, int focused) {
    SDL_Event e;
    memset(&e, 0, sizeof e);
    e.type = SDL_WINDOWEVENT;
    e.window.event = focused ? SDL_WINDOWEVENT_FOCUS_GAINED : SDL_WINDOWEVENT_FOCUS_LOST;
    (void)ls_input(a, &e);
}
static void sample(LS_App *a, LS_InputState *s, uint32_t now) {
    ls_input_poll(a, s);
    ls_lifecycle_update(&a->lifecycle, s->held, now);
}
static void pad_test(LS_App *a, SDL_JoystickType type) {
    LS_InputState s = {0};
    int index = SDL_JoystickAttachVirtual(type, 6, 16, 0);
    assert(index >= 0); drain(a);
    SDL_Joystick *pad = SDL_JoystickOpen(index);
    assert(pad);
    int mapped = SDL_IsGameController(index);
    int start = mapped ? SDL_CONTROLLER_BUTTON_START : 7;
    int select = mapped ? SDL_CONTROLLER_BUTTON_BACK : 6;
    ls_lifecycle_init(&a->lifecycle, 100);
    /* Start on one source, Select on a different source. */
    key(a, SDLK_RETURN, 1);
    sample(a, &s, 10);
    assert((s.held & LS_EXIT_COMBO) == LS_BUTTON_START && !a->lifecycle.combo_active);
    assert(!SDL_JoystickSetVirtualButton(pad, select, 1)); drain(a);
    sample(a, &s, 20);
    assert((s.held & LS_EXIT_COMBO) == LS_EXIT_COMBO);
    assert(s.pressed & LS_BUTTON_SELECT);
    sample(a, &s, 119); assert(!a->lifecycle.exit_requested);
    assert(!SDL_JoystickSetVirtualButton(pad, select, 0)); drain(a);
    sample(a, &s, 120);
    assert((s.released & LS_BUTTON_SELECT) && !a->lifecycle.combo_active);
    assert(!SDL_JoystickSetVirtualButton(pad, select, 1)); drain(a);
    sample(a, &s, 130); sample(a, &s, 230);
    assert(ls_lifecycle_take_exit(&a->lifecycle) == LS_EXIT_HELD_COMBO);
    sample(a, &s, 400); assert(!a->lifecycle.exit_requested);
    key(a, SDLK_RETURN, 0);
    assert(!SDL_JoystickSetVirtualButton(pad, start, 1)); drain(a);
    sample(a, &s, 410);
    assert((s.held & LS_EXIT_COMBO) == LS_EXIT_COMBO);
    /* Loss+gain in one event drain must still reset the old hold. */
    focus(a, 0); key(a, SDLK_RETURN, 1); focus(a, 1);
    assert(!a->input_keys[SDL_SCANCODE_RETURN]);
    sample(a, &s, 500); sample(a, &s, 599);
    assert(!a->lifecycle.exit_requested);
    assert(!SDL_JoystickDetachVirtual(index)); drain(a);
    SDL_JoystickClose(pad);
    sample(a, &s, 600);
    assert(!(s.held & LS_EXIT_COMBO) && (s.released & LS_EXIT_COMBO) == LS_EXIT_COMBO);
    assert(!a->lifecycle.combo_active && !a->lifecycle.exit_requested);
    for (int i = 0; i < LS_INPUT_PADS; ++i) assert(!s.pads[i].connected);
    /* Reconnect has no remembered buttons or progress. */
    index = SDL_JoystickAttachVirtual(type, 6, 16, 0);
    assert(index >= 0); drain(a); sample(a, &s, 2000);
    assert(!(s.held & LS_EXIT_COMBO) && !a->lifecycle.exit_requested);
    assert(!SDL_JoystickDetachVirtual(index)); drain(a);
}
int main(void) {
    LS_App a;
    LS_InputState s = {0};
    setenv("SDL_VIDEODRIVER", "dummy", 1);
    setenv("SDL_AUDIODRIVER", "dummy", 1);
    unsetenv("LILSMOKE_OD_NATIVE");
    assert(!ls_app_open(&a, "Lifecycle test", 0)); drain(&a);
    focus(&a, 1);
    pad_test(&a, SDL_JOYSTICK_TYPE_UNKNOWN);
    pad_test(&a, SDL_JOYSTICK_TYPE_GAMECONTROLLER);
    a.native_keys = 1;
    ls_lifecycle_init(&a.lifecycle, 100);
    key(&a, SDLK_RETURN, 1); key(&a, SDLK_RSHIFT, 1);
    sample(&a, &s, 10); sample(&a, &s, 109);
    assert((s.held & LS_EXIT_COMBO) == LS_EXIT_COMBO && !a.lifecycle.exit_requested);
    key(&a, SDLK_ESCAPE, 1); key(&a, SDLK_RSHIFT, 0);
    sample(&a, &s, 110);
    assert(ls_lifecycle_take_exit(&a.lifecycle) == LS_EXIT_HELD_COMBO);
    key(&a, SDLK_ESCAPE, 0); key(&a, SDLK_ESCAPE, 1);
    sample(&a, &s, 120);
    assert(a.lifecycle.combo_held_ms == 0 && !a.lifecycle.exit_requested);
    /* An overlapping alias remains held, so its release cannot restart time. */
    key(&a, SDLK_RSHIFT, 1); key(&a, SDLK_ESCAPE, 0);
    sample(&a, &s, 219);
    assert(a.lifecycle.combo_held_ms == 99 && !a.lifecycle.exit_requested);
    sample(&a, &s, 220);
    assert(ls_lifecycle_take_exit(&a.lifecycle) == LS_EXIT_HELD_COMBO);
    focus(&a, 0); sample(&a, &s, 1000);
    assert(!s.held && !a.lifecycle.combo_active);
    focus(&a, 1); ls_app_poll(&a, &s); assert(!s.held);
    SDL_Event e; memset(&e, 0, sizeof e); e.type = SDL_QUIT;
    assert(ls_input(&a, &e) == LS_QUIT && a.lifecycle.exit_requested == LS_EXIT_SDL_QUIT);
    assert(ls_lifecycle_take_exit(&a.lifecycle) == LS_EXIT_SDL_QUIT);
    e.type = SDL_WINDOWEVENT; e.window.event = SDL_WINDOWEVENT_CLOSE;
    assert(ls_input(&a, &e) == LS_QUIT && a.lifecycle.exit_requested == LS_EXIT_SDL_QUIT);
    LS_FrameClock c;
    assert(!ls_frame_init(&c, 5, SDL_GetTicks()));
    for (int i = 0; i < 3; ++i) ls_frame_pace(&c);
    assert(c.stats.frames == 3 && c.stats.window_sum_ms >= 15);
    /* No production lifecycle call injects a host key/event. */
    while (SDL_PollEvent(&e)) assert(e.type != SDL_KEYDOWN && e.type != SDL_KEYUP);
    ls_app_close(&a);
    puts("PASS SDL raw/mapped split-source held/release/reconnect/focus/unplug/native/quit; real delay pacing; no injected keys");
    return 0;
}
