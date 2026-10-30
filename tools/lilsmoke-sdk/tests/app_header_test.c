/* SPDX-License-Identifier: MPL-2.0
 * Copyright (c) 2026 Jose Pilas.
 */
#define _POSIX_C_SOURCE 200809L
#include <lilsmoke/app.h>
#include <lilsmoke/input.h>
#include <assert.h>

int main(void) {
    LS_InputState input;
    memset(&input, 0, sizeof input);
    ls_input_update(&input, LS_BUTTON_LEFT | LS_BUTTON_ACTIVATE);
    assert(input.pressed == input.held && !input.released);
    ls_input_update(&input, LS_BUTTON_LEFT);
    assert(!input.pressed && input.released == LS_BUTTON_ACTIVATE);
    ls_input_update(&input, 0);
    assert(!input.held && input.released == LS_BUTTON_LEFT);
    assert(ls_axis_normalize(-32768) == -1.0f && ls_axis_normalize(32767) == 1.0f);
    assert(ls_axis_normalize(0) == 0.0f);
    char output[8192];
    unsetenv("LILSMOKE_OPEN_CORE"); unsetenv("LILSMOKE_OPEN_ROM");
    unsetenv("LILSMOKE_ROM_ROOT"); unsetenv("LILSMOKE_APP_ID");
    assert(!lilsmoke_app_id());
    assert(lilsmoke_open_rom_path(output, sizeof output) == 1 && !*output);
    setenv("LILSMOKE_APP_ID", "org.example.native", 1);
    assert(!strcmp(lilsmoke_app_id(), "org.example.native"));
    setenv("LILSMOKE_OPEN_CORE", "gba", 1);
    assert(lilsmoke_open_rom_path(output, sizeof output) == -1);
    setenv("LILSMOKE_OPEN_ROM", "sub/Game ; '$literal'.gba", 1);
    assert(!lilsmoke_open_rom_path(output, sizeof output));
    assert(!strcmp(output, "/storage/roms/gba/sub/Game ; '$literal'.gba"));
    assert(lilsmoke_open_rom_path(output, 5) == -1 && !*output);
    setenv("LILSMOKE_ROM_ROOT", "/", 1);
    assert(!lilsmoke_open_rom_path(output, sizeof output));
    assert(!strcmp(output, "/gba/sub/Game ; '$literal'.gba"));
    setenv("LILSMOKE_ROM_ROOT", "/custom///", 1);
    assert(!lilsmoke_open_rom_path(output, sizeof output));
    assert(!strcmp(output, "/custom/gba/sub/Game ; '$literal'.gba"));
    const char *bad[] = {"../escape", "/absolute", "a//b", "a/./b", "a/../b", "a\\b", "x\nbad", ""};
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; ++i) {
        setenv("LILSMOKE_OPEN_ROM", bad[i], 1);
        assert(lilsmoke_open_rom_path(output, sizeof output) == -1 && !*output);
    }
    setenv("LILSMOKE_OPEN_ROM", "game.gba", 1);
    setenv("LILSMOKE_OPEN_CORE", "../gba", 1);
    assert(lilsmoke_open_rom_path(output, sizeof output) == -1);
    assert(lilsmoke_open_rom_path(NULL, 0) == -1);
    puts("PASS app.h: borrowed environment, normal menu, lexical ROM path and bounds");
    return 0;
}
