/* SPDX-License-Identifier: MPL-2.0
 * Copyright (c) 2026 Jose Pilas.
 * Source-level SDL2/POSIX conveniences, not OS services or a binary ABI.
 */
#ifndef LILSMOKE_SDK_SDL_H
#define LILSMOKE_SDK_SDL_H
#include <SDL.h>
#include <stdint.h>
#include <stddef.h>
#include <lilsmoke/input.h>
#include <lilsmoke/lifecycle.h>
#ifdef __cplusplus
extern "C" {
#endif
#define LS_WIDTH 640
#define LS_HEIGHT 480
#define LS_CAPTURE_FRAMES 44100

typedef enum {
    LS_NONE, LS_UP, LS_DOWN, LS_LEFT, LS_RIGHT, LS_ACTIVATE,
    LS_BACK, LS_MENU, LS_PAUSE, LS_QUIT, LS_POINTER
} LS_Action;
typedef struct { int color, tone, volume; } LS_Settings;
typedef struct {
    SDL_GameController *controller;
    SDL_Joystick *joystick;
    SDL_JoystickID id;
    int x, y;
    Uint8 hat;
    uint32_t combo_held;
} LS_Pad;
typedef struct {
    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_AudioDeviceID audio;
    LS_Pad pads[8];
    uint32_t phase;
    int pointer_x, pointer_y, audio_active, audio_failed, native_keys;
    int16_t *capture;
    size_t capture_frames;
    uint64_t frames, events, keyboard_events, joystick_events, mouse_events, touch_events;
    uint64_t tone_queued, nonzero_samples;
    Uint8 input_keys[SDL_NUM_SCANCODES];
    int input_focus_lost;
    LS_Lifecycle lifecycle;
} LS_App;

int ls_app_open(LS_App *app, const char *title, int capture_audio);
void ls_app_close(LS_App *app);
LS_Action ls_input(LS_App *app, const SDL_Event *event);
/* Drain events through ls_input first, then sample once per frame. Focus loss
 * clears held buttons/axes; disconnected pads release on the next snapshot.
 * Native M8 keyboard adapter supplies digital keys only, never analog floats. */
void ls_input_poll(LS_App *app, LS_InputState *state);
/* Poll aggregate held input and advance lifecycle using SDL's monotonic ticks.
 * Drain ls_input events first. Default held-combo delay is 1500 ms. */
void ls_app_poll(LS_App *app, LS_InputState *state);
/* Wait (SDL_Delay, never spin) to the next deadline and record work/total time.
 * Initialize the clock with ls_frame_init(..., SDL_GetTicks()) first. */
void ls_frame_pace(LS_FrameClock *clock);
void ls_begin(LS_App *app, uint32_t rgb);
void ls_fill(LS_App *app, int x, int y, int w, int h, uint32_t rgb);
void ls_line(LS_App *app, int x1, int y1, int x2, int y2, uint32_t rgb);
void ls_text(LS_App *app, const char *text, int x, int y, int scale, uint32_t rgb);
void ls_present(LS_App *app);
/* Capture the current backbuffer AFTER drawing and BEFORE ls_present().
 * SDL invalidates it after Present; redraw before a later capture. */
int ls_screenshot(LS_App *app, const char *bmp_path);
int ls_audio_pump(LS_App *app, int enabled, int volume);
int ls_audio_wav(LS_App *app, const char *path);
/* Single safe leaf under APP_DIR/userdata, or cwd/userdata for direct host use.
 * The launcher/environment and parent directories must be trusted. */
int ls_data_path(char *output, size_t capacity, const char *leaf);
/* load: 1 valid settings, 0 missing, -1 corrupt/error (defaults retained). */
int ls_settings_load(LS_Settings *settings);
int ls_settings_save(const LS_Settings *settings);
#ifdef __cplusplus
}
#endif
#endif
